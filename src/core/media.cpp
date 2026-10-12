#include "mediabridge/media.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_desc.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#ifdef __ANDROID__
#include <fcntl.h>
#include <sys/syscall.h>
#endif
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(61, 19, 100)
#error MediaBridge requires FFmpeg 7.1 or newer
#endif

namespace mediabridge {
namespace {
namespace fs = std::filesystem;
constexpr AVRational microseconds{1, AV_TIME_BASE};

void check(int ret, const std::string& action) {
    if (ret >= 0) return;
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(ret, buffer, sizeof(buffer));
    throw Error(action + ": " + buffer);
}
void cancelled(const std::atomic_bool* flag) {
    if (flag && flag->load(std::memory_order_relaxed)) throw Cancelled();
}
int interrupt(void* opaque) {
    return opaque && static_cast<const std::atomic_bool*>(opaque)->load(std::memory_order_relaxed);
}

struct Input {
    AVFormatContext* ctx = nullptr;
    explicit Input(const std::string& path, const std::atomic_bool* cancel) {
        if (!fs::is_regular_file(fs::u8path(path))) throw Error("Input must be a local regular file");
        ctx = avformat_alloc_context();
        if (!ctx) throw Error("Cannot allocate input context");
        ctx->interrupt_callback = {interrupt, const_cast<std::atomic_bool*>(cancel)};
        AVDictionary* opts = nullptr;
        // The entry point accepts local files; no network protocols are enabled.
        av_dict_set(&opts, "protocol_whitelist", "file", 0);
        int ret = avformat_open_input(&ctx, path.c_str(), nullptr, &opts);
        av_dict_free(&opts);
        try {
            cancelled(cancel);
            check(ret, "Open input");
            check(avformat_find_stream_info(ctx, nullptr), "Read stream information");
            cancelled(cancel);
        } catch (...) {
            avformat_close_input(&ctx);
            throw;
        }
    }
    ~Input() { avformat_close_input(&ctx); }
    Input(const Input&) = delete;
};

struct Output {
    AVFormatContext* ctx = nullptr;
    ~Output() {
        if (ctx) {
            if (!(ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&ctx->pb);
            avformat_free_context(ctx);
        }
    }
};

struct PacketDeleter { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct FrameDeleter { void operator()(AVFrame* f) const { av_frame_free(&f); } };
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
Packet makePacket() {
    Packet p(av_packet_alloc());
    if (!p) throw Error("Cannot allocate packet");
    return p;
}
Frame makeFrame() {
    Frame f(av_frame_alloc());
    if (!f) throw Error("Cannot allocate frame");
    return f;
}

class StagedFile {
    fs::path destination_, directory_, file_;
public:
    explicit StagedFile(const Options& options) : destination_(fs::absolute(fs::u8path(options.output))) {
        if (!options.overwrite && fs::exists(destination_)) throw Error("Output already exists; choose a new file or use --overwrite");
        if (!fs::is_directory(destination_.parent_path())) throw Error("Output directory does not exist");
        std::random_device random;
        for (int attempt = 0; attempt < 20; ++attempt) {
            directory_ = destination_.parent_path() / (".mediabridge-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (fs::create_directory(directory_)) {
                file_ = directory_ / "output";
                return;
            }
        }
        throw Error("Cannot create temporary output directory");
    }
    ~StagedFile() {
        std::error_code ignored;
        fs::remove_all(directory_, ignored);
    }
    std::string path() const { return file_.u8string(); }
    void commit(bool overwrite) {
#ifdef _WIN32
        DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!MoveFileExW(file_.c_str(), destination_.c_str(), flags))
            throw Error("Commit output: " + std::system_category().message(GetLastError()));
#else
        if (overwrite) {
            fs::rename(file_, destination_);
        } else {
#ifdef __ANDROID__
            // Android app storage may forbid hard links. renameat2 keeps the same atomic no-clobber contract.
            if (::syscall(SYS_renameat2, AT_FDCWD, file_.c_str(), AT_FDCWD, destination_.c_str(), 1 /* RENAME_NOREPLACE */) != 0)
                throw Error("Commit output: " + std::generic_category().message(errno));
#else
            // link() is atomic and fails with EEXIST; rename() alone could clobber a racing writer.
            if (::link(file_.c_str(), destination_.c_str()) != 0)
                throw Error("Commit output: " + std::generic_category().message(errno));
            fs::remove(file_);
#endif
        }
#endif
    }
};

struct Pipeline {
    AVStream* input = nullptr;
    AVStream* output = nullptr;
    AVCodecContext* decoder = nullptr;
    AVCodecContext* encoder = nullptr;
    AVFilterGraph* graph = nullptr;
    AVFilterContext* source = nullptr;
    AVFilterContext* sink = nullptr;
    Frame decoded = makeFrame();
    Frame filtered = makeFrame();
    Packet encoded = makePacket();
    int64_t fallbackPts = 0;
    int64_t lastPts = AV_NOPTS_VALUE;
    bool pastEnd = false;
    int rotation = 0;
    double duration = 0;
    ~Pipeline() {
        avfilter_graph_free(&graph);
        avcodec_free_context(&decoder);
        avcodec_free_context(&encoder);
    }
};

std::string decimal(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(9) << value;
    return out.str();
}

void configureGraph(Pipeline& p, const Options& options) {
    p.graph = avfilter_graph_alloc();
    if (!p.graph) throw Error("Cannot allocate filter graph");
    p.graph->nb_threads = options.threads;
    const bool video = p.decoder->codec_type == AVMEDIA_TYPE_VIDEO;
    std::ostringstream args;
    args.imbue(std::locale::classic());
    args << "time_base=" << p.input->time_base.num << '/' << p.input->time_base.den;
    std::string filter;
    if (video) {
        auto aspect = p.decoder->sample_aspect_ratio;
        if (aspect.num <= 0 || aspect.den <= 0) aspect = {1, 1};
        args << ":video_size=" << p.decoder->width << 'x' << p.decoder->height
             << ":pix_fmt=" << p.decoder->pix_fmt << ":pixel_aspect=" << aspect.num << '/' << aspect.den;
        filter = "trim=start=" + decimal(options.start);
    } else {
        char layout[256]{};
        check(av_channel_layout_describe(&p.decoder->ch_layout, layout, sizeof(layout)), "Describe channel layout");
        const char* format = av_get_sample_fmt_name(p.decoder->sample_fmt);
        if (!format) throw Error("Decoder did not expose an audio sample format");
        args << ":sample_rate=" << p.decoder->sample_rate << ":sample_fmt=" << format << ":channel_layout=" << layout;
        filter = "atrim=start=" + decimal(options.start);
    }
    if (options.duration > 0) filter += ":end=" + decimal(options.start + options.duration);
    filter += video ? ",setpts=(PTS-" : ",asetpts=(PTS-";
    filter += decimal(options.start) + "/TB)/" + decimal(options.speed);
    if (video) {
        if (options.deinterlace) filter += ",yadif";
        if (options.cropWidth > 0)
            filter += ",crop=" + std::to_string(options.cropWidth) + ':' + std::to_string(options.cropHeight)
                + ':' + std::to_string(options.cropX) + ':' + std::to_string(options.cropY);
        if (p.rotation == 90) filter += ",transpose=clock";
        if (p.rotation == 180) filter += ",hflip,vflip";
        if (p.rotation == 270) filter += ",transpose=cclock";
        if (options.flipHorizontal) filter += ",hflip";
        if (options.flipVertical) filter += ",vflip";
        std::string size = std::to_string(p.encoder->width) + ':' + std::to_string(p.encoder->height);
        if (options.width && options.height && options.fit == "fit")
            filter += ",scale=" + size + ":force_original_aspect_ratio=decrease:force_divisible_by=2:flags=bicubic,pad="
                + size + ":(ow-iw)/2:(oh-ih)/2";
        else if (options.width && options.height && options.fit == "fill")
            filter += ",scale=" + size + ":force_original_aspect_ratio=increase:force_divisible_by=2:flags=bicubic,crop=" + size;
        else filter += ",scale=" + size + ":flags=bicubic";
        if (options.brightness != 0 || options.contrast != 1 || options.saturation != 1)
            filter += ",eq=brightness=" + decimal(options.brightness) + ":contrast=" + decimal(options.contrast)
                + ":saturation=" + decimal(options.saturation);
        if (options.fps > 0) filter += ",fps=" + decimal(options.fps);
        if (options.fadeIn > 0) filter += ",fade=t=in:st=0:d=" + decimal(options.fadeIn);
        if (options.fadeOut > 0) filter += ",fade=t=out:st=" + decimal(std::max(0.0, p.duration / options.speed - options.fadeOut))
                                           + ":d=" + decimal(options.fadeOut);
        const char* pix = av_get_pix_fmt_name(p.encoder->pix_fmt);
        if (!pix) throw Error("Encoder pixel format is unavailable");
        filter += ",format=pix_fmts=" + std::string(pix);
    } else {
        double tempo = options.speed;
        while (tempo > 2) { filter += ",atempo=2"; tempo /= 2; }
        while (tempo < 0.5) { filter += ",atempo=0.5"; tempo /= 0.5; }
        if (tempo != 1) filter += ",atempo=" + decimal(tempo);
        if (options.normalizeAudio) filter += ",loudnorm=I=" + decimal(options.loudness) + ":TP=-1.5:LRA=11";
        if (options.volumeDb != 0) filter += ",volume=" + decimal(options.volumeDb) + "dB";
        if (options.fadeIn > 0) filter += ",afade=t=in:st=0:d=" + decimal(options.fadeIn);
        if (options.fadeOut > 0) filter += ",afade=t=out:st=" + decimal(std::max(0.0, p.duration / options.speed - options.fadeOut))
                                            + ":d=" + decimal(options.fadeOut);
        char outputLayout[256]{};
        check(av_channel_layout_describe(&p.encoder->ch_layout, outputLayout, sizeof(outputLayout)), "Describe output layout");
        filter += ",aformat=sample_fmts=" + std::string(av_get_sample_fmt_name(p.encoder->sample_fmt))
            + ":sample_rates=" + std::to_string(p.encoder->sample_rate) + ":channel_layouts=" + outputLayout;
    }
    check(avfilter_graph_create_filter(&p.source, avfilter_get_by_name(video ? "buffer" : "abuffer"),
          "input", args.str().c_str(), nullptr, p.graph), "Create input filter");
    check(avfilter_graph_create_filter(&p.sink, avfilter_get_by_name(video ? "buffersink" : "abuffersink"),
          "output", nullptr, nullptr, p.graph), "Create output filter");
    AVFilterInOut* inputs = avfilter_inout_alloc();
    AVFilterInOut* outputs = avfilter_inout_alloc();
    if (!inputs || !outputs) {
        avfilter_inout_free(&inputs); avfilter_inout_free(&outputs);
        throw Error("Cannot allocate filter endpoints");
    }
    inputs->name = av_strdup("out"); inputs->filter_ctx = p.sink; inputs->pad_idx = 0;
    outputs->name = av_strdup("in"); outputs->filter_ctx = p.source; outputs->pad_idx = 0;
    int ret = avfilter_graph_parse_ptr(p.graph, filter.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs); avfilter_inout_free(&outputs);
    check(ret, "Parse filters");
    check(avfilter_graph_config(p.graph, nullptr), "Configure filters");
    if (!video && p.encoder->frame_size > 0) av_buffersink_set_frame_size(p.sink, p.encoder->frame_size);
}

std::unique_ptr<Pipeline> configurePipeline(AVFormatContext* input, AVFormatContext* output,
                                          int index, const Options& options, const Preset& format) {
    auto p = std::make_unique<Pipeline>();
    p->input = input->streams[index];
    const AVCodec* decoder = avcodec_find_decoder(p->input->codecpar->codec_id);
    if (!decoder) throw Error("Decoder is unavailable for input stream " + std::to_string(index));
    p->decoder = avcodec_alloc_context3(decoder);
    if (!p->decoder) throw Error("Cannot allocate decoder");
    check(avcodec_parameters_to_context(p->decoder, p->input->codecpar), "Configure decoder");
    p->decoder->pkt_timebase = p->input->time_base;
    p->decoder->thread_count = options.threads;
    check(avcodec_open2(p->decoder, decoder, nullptr), "Open decoder");
    const bool video = p->decoder->codec_type == AVMEDIA_TYPE_VIDEO;
    std::string name = video ? (options.videoEncoder.empty() ? format.videoEncoder : options.videoEncoder) : (options.audioEncoder.empty() ? format.audioEncoder : options.audioEncoder);
    const AVCodec* encoder = avcodec_find_encoder_by_name(name.c_str());
    if (!encoder) throw Error("Encoder '" + name + "' is not included in this FFmpeg build");
    if (encoder->type != p->decoder->codec_type) throw Error("Encoder type does not match stream");
    p->encoder = avcodec_alloc_context3(encoder);
    if (!p->encoder) throw Error("Cannot allocate encoder");
    auto& enc = *p->encoder;
    enc.thread_count = options.threads;
    double inputDuration = input->duration == AV_NOPTS_VALUE ? 0 : input->duration / double(AV_TIME_BASE);
    p->duration = inputDuration > 0 ? std::max(0.0, inputDuration - options.start) : 0;
    if (options.duration > 0) p->duration = p->duration > 0 ? std::min(p->duration, options.duration) : options.duration;
    if (options.fadeOut > 0 && p->duration <= 0) throw Error("Fade-out requires a known duration");
    if (video) {
        if (p->decoder->width <= 0 || p->decoder->height <= 0 || p->decoder->pix_fmt == AV_PIX_FMT_NONE)
            throw Error("Video dimensions or pixel format are unavailable");
        int sourceWidth = options.cropWidth ? options.cropWidth : p->decoder->width;
        int sourceHeight = options.cropHeight ? options.cropHeight : p->decoder->height;
        if (options.cropWidth && (int64_t(options.cropX) + sourceWidth > p->decoder->width ||
            int64_t(options.cropY) + sourceHeight > p->decoder->height)) throw Error("Crop is outside input dimensions");
        p->rotation = options.rotation;
        if (options.autoRotate) {
            const auto* matrix = av_packet_side_data_get(p->input->codecpar->coded_side_data,
                p->input->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
            if (matrix && matrix->size >= 9 * sizeof(int32_t)) {
                double angle = av_display_rotation_get(reinterpret_cast<const int32_t*>(matrix->data));
                if (std::isfinite(angle)) p->rotation = (p->rotation - int(std::llround(angle / 90)) * 90 + 720) % 360;
            }
        }
        if (p->rotation == 90 || p->rotation == 270) std::swap(sourceWidth, sourceHeight);
        auto even = [](double value) { return std::max(2, int(std::llround(value / 2)) * 2); };
        enc.width = options.width ? options.width :
            (options.height ? even(double(sourceWidth) * options.height / sourceHeight) : even(sourceWidth));
        enc.height = options.height ? options.height : even(double(sourceHeight) * enc.width / sourceWidth);
        const AVPixelFormat* pixels = nullptr;
        check(avcodec_get_supported_config(p->encoder, encoder, AV_CODEC_CONFIG_PIX_FORMAT, 0,
            reinterpret_cast<const void**>(&pixels), nullptr), "Query encoder pixel formats");
        enc.pix_fmt = AV_PIX_FMT_YUV420P;
        if (pixels) {
            bool supported = false;
            for (const auto* px = pixels; *px != AV_PIX_FMT_NONE; ++px) if (*px == AV_PIX_FMT_YUV420P) supported = true;
            if (!supported) enc.pix_fmt = pixels[0];
        }
        enc.color_primaries = p->decoder->color_primaries;
        enc.color_trc = p->decoder->color_trc;
        enc.colorspace = p->decoder->colorspace;
        enc.color_range = p->decoder->color_range;
        enc.chroma_sample_location = p->decoder->chroma_sample_location;
        enc.sample_aspect_ratio = p->decoder->sample_aspect_ratio;
        if (enc.sample_aspect_ratio.num <= 0) enc.sample_aspect_ratio = {1, 1};
        if (p->rotation == 90 || p->rotation == 270) enc.sample_aspect_ratio = av_inv_q(enc.sample_aspect_ratio);
        enc.framerate = options.fps > 0 ? av_d2q(options.fps, 100000) : av_guess_frame_rate(input, p->input, nullptr);
        if (enc.framerate.num <= 0 || enc.framerate.den <= 0) enc.framerate = {25, 1};
        // Microsecond-ish precision for VFR; MPEG-4 requires denominator <= 65535.
        enc.time_base = name == "mpeg2video" ? av_inv_q(enc.framerate) : name == "mpeg4" ? AVRational{1, 60000} : AVRational{1, 90000};
        enc.bit_rate = int64_t(options.videoKbps) * 1000;
        enc.gop_size = options.gop;
        enc.max_b_frames = 0;
    } else {
        const AVSampleFormat* samples = nullptr;
        check(avcodec_get_supported_config(p->encoder, encoder, AV_CODEC_CONFIG_SAMPLE_FORMAT,
                                          0, reinterpret_cast<const void**>(&samples), nullptr), "Query audio sample formats");
        enc.sample_fmt = samples ? samples[0] : p->decoder->sample_fmt;
        const int* rates = nullptr;
        int count = 0;
        check(avcodec_get_supported_config(p->encoder, encoder, AV_CODEC_CONFIG_SAMPLE_RATE,
                                          0, reinterpret_cast<const void**>(&rates), &count), "Query audio sample rates");
        enc.sample_rate = options.sampleRate ? options.sampleRate : p->decoder->sample_rate;
        if (rates && count > 0) {
            enc.sample_rate = rates[0];
            int requested = options.sampleRate ? options.sampleRate : p->decoder->sample_rate;
            for (int i = 0; i < count; ++i)
                if (std::abs(rates[i] - requested) < std::abs(enc.sample_rate - requested)) enc.sample_rate = rates[i];
            if (options.sampleRate && enc.sample_rate != requested) throw Error("Encoder does not support the requested sample rate");
        }
        if (enc.sample_rate <= 0) throw Error("Audio sample rate is unavailable");

        if (!av_channel_layout_check(&p->decoder->ch_layout))
            av_channel_layout_default(&p->decoder->ch_layout, std::max(1, p->input->codecpar->ch_layout.nb_channels));
        if (options.channels) av_channel_layout_default(&enc.ch_layout, options.channels);
        else check(av_channel_layout_copy(&enc.ch_layout, &p->decoder->ch_layout), "Copy channel layout");
        enc.time_base = {1, enc.sample_rate};
        enc.bit_rate = int64_t(options.audioKbps) * 1000;
    }
    if (output->oformat->flags & AVFMT_GLOBALHEADER) enc.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* codecOptions = nullptr;
    if (name == "libx264" || name == "libx265") av_dict_set(&codecOptions, "preset", options.encoderPreset.c_str(), 0);
    if (options.crf >= 0 && video) {
        if (name != "libx264" && name != "libx265" && name != "libvpx-vp9" && name != "libaom-av1" && name != "libsvtav1")
            throw Error("CRF is not supported by this encoder");
        av_dict_set(&codecOptions, "crf", std::to_string(options.crf).c_str(), 0);
        enc.bit_rate = 0;
    }
    if (name == "libvpx-vp9") av_dict_set(&codecOptions, "deadline", "realtime", 0);
    int ret = avcodec_open2(p->encoder, encoder, &codecOptions);
    av_dict_free(&codecOptions);
    check(ret, "Open encoder " + name);
    p->output = avformat_new_stream(output, nullptr);
    if (!p->output) throw Error("Cannot create output stream");
    p->output->time_base = enc.time_base;
    p->output->avg_frame_rate = enc.framerate;
    p->output->sample_aspect_ratio = enc.sample_aspect_ratio;
    check(avcodec_parameters_from_context(p->output->codecpar, p->encoder), "Copy encoder parameters");
    if (video && !options.autoRotate) {
        const auto* matrix = av_packet_side_data_get(p->input->codecpar->coded_side_data,
            p->input->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
        if (matrix) {
            auto* copied = av_packet_side_data_new(&p->output->codecpar->coded_side_data,
                &p->output->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX, matrix->size, 0);
            if (!copied) throw Error("Cannot copy video rotation matrix");
            std::memcpy(copied->data, matrix->data, matrix->size);
        }
    }
    if (options.copyMetadata) av_dict_copy(&p->output->metadata, p->input->metadata, 0);
    p->output->disposition = p->input->disposition & ~AV_DISPOSITION_ATTACHED_PIC;
    configureGraph(*p, options);
    return p;
}

void writeEncoded(Pipeline& p, AVFormatContext* output, std::atomic_bool& cancel, int64_t& packets) {
    for (;;) {
        cancelled(&cancel);
        int ret = avcodec_receive_packet(p.encoder, p.encoded.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return;
        check(ret, "Receive encoded packet");
        av_packet_rescale_ts(p.encoded.get(), p.encoder->time_base, p.output->time_base);
        p.encoded->stream_index = p.output->index;
        p.encoded->pos = -1;
        check(av_interleaved_write_frame(output, p.encoded.get()), "Write encoded packet");
        av_packet_unref(p.encoded.get());
        ++packets;
    }
}

void encode(Pipeline& p, AVFrame* frame, AVFormatContext* output, std::atomic_bool& cancel, int64_t& packets) {
    if (frame) {
        frame->pts = av_rescale_q(frame->pts, av_buffersink_get_time_base(p.sink), p.encoder->time_base);
        if (p.lastPts != AV_NOPTS_VALUE && frame->pts <= p.lastPts)
            throw Error("Input frame timestamps are not strictly increasing");
        p.lastPts = frame->pts;
        frame->pict_type = AV_PICTURE_TYPE_NONE;
    }
    int ret = avcodec_send_frame(p.encoder, frame);
    if (ret == AVERROR(EAGAIN)) {
        writeEncoded(p, output, cancel, packets);
        ret = avcodec_send_frame(p.encoder, frame);
    }
    check(ret, "Send frame to encoder");
    writeEncoded(p, output, cancel, packets);
}

void drainFilter(Pipeline& p, AVFormatContext* output, std::atomic_bool& cancel, int64_t& packets) {
    for (;;) {
        cancelled(&cancel);
        int ret = av_buffersink_get_frame(p.sink, p.filtered.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return;
        check(ret, "Read filtered frame");
        encode(p, p.filtered.get(), output, cancel, packets);
        av_frame_unref(p.filtered.get());
    }
}

void drainDecoder(Pipeline& p, AVFormatContext* output, const Options& options,
                  int64_t origin, std::atomic_bool& cancel, int64_t& packets) {
    for (;;) {
        cancelled(&cancel);
        int ret = avcodec_receive_frame(p.decoder, p.decoded.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return;
        check(ret, "Decode frame");
        auto& frame = *p.decoded;
        frame.pts = frame.best_effort_timestamp;
        if (frame.pts == AV_NOPTS_VALUE) frame.pts = p.fallbackPts;
        int64_t step = frame.nb_samples > 0
            ? av_rescale_q(frame.nb_samples, AVRational{1, p.decoder->sample_rate}, p.input->time_base)
            : av_rescale_q(1, av_inv_q(p.encoder->framerate), p.input->time_base);
        p.fallbackPts = frame.pts + std::max<int64_t>(1, step);
        frame.pts -= av_rescale_q(origin, microseconds, p.input->time_base);
        if (options.duration > 0 && frame.pts * av_q2d(p.input->time_base) >= options.start + options.duration)
            p.pastEnd = true;
        int filtered = av_buffersrc_add_frame_flags(p.source, &frame, AV_BUFFERSRC_FLAG_KEEP_REF);
        if (filtered == AVERROR_EOF) p.pastEnd = true;
        else check(filtered, "Filter decoded frame");
        av_frame_unref(p.decoded.get());
        drainFilter(p, output, cancel, packets);
    }
}

void decode(Pipeline& p, AVPacket* packet, AVFormatContext* output, const Options& options,
            int64_t origin, std::atomic_bool& cancel, int64_t& packets) {
    int ret = avcodec_send_packet(p.decoder, packet);
    if (ret == AVERROR(EAGAIN)) {
        drainDecoder(p, output, options, origin, cancel, packets);
        ret = avcodec_send_packet(p.decoder, packet);
    }
    check(ret, "Send packet to decoder");
    drainDecoder(p, output, options, origin, cancel, packets);
}
} // namespace

const std::vector<Preset>& presets() {
    static const std::vector<Preset> values{
        {"mp4", "MP4 视频", "mp4", "mp4", "mpeg4", "aac", false},
        {"mkv", "MKV 视频", "mkv", "matroska", "mpeg4", "aac", false},
        {"webm", "WebM 视频", "webm", "webm", "libvpx-vp9", "libopus", false},
        {"mov", "MOV 视频", "mov", "mov", "mpeg4", "aac", false},
        {"avi", "AVI 视频", "avi", "avi", "mpeg4", "libmp3lame", false},
        {"ts", "MPEG-TS 视频", "ts", "mpegts", "mpeg2video", "mp2", false},
        {"ogg", "Ogg 音频", "ogg", "ogg", "", "libvorbis", true},
        {"opus", "Opus 音频", "opus", "opus", "", "libopus", true},
        {"m4a", "M4A 音频", "m4a", "ipod", "", "aac", true},
        {"mp3", "MP3 音频", "mp3", "mp3", "", "libmp3lame", true},
        {"wav", "WAV 音频", "wav", "wav", "", "pcm_s16le", true},
        {"flac", "FLAC 音频", "flac", "flac", "", "flac", true}
    };
    return values;
}
const Preset& preset(const std::string& id) {
    for (const auto& value : presets()) if (value.id == id) return value;
    throw Error("Unknown format: " + id);
}
bool encoderAvailable(const std::string& name) {
    return name.empty() || avcodec_find_encoder_by_name(name.c_str());
}
bool encoderCompatible(const std::string& presetId, const std::string& name) {
    const auto& format = preset(presetId);
    const auto* codec = avcodec_find_encoder_by_name(name.c_str());
    const auto* muxer = av_guess_format(format.container.c_str(), nullptr, nullptr);
    if (!codec || !muxer || (format.audioOnly && codec->type == AVMEDIA_TYPE_VIDEO)) return false;
    return avformat_query_codec(muxer, codec->id, FF_COMPLIANCE_NORMAL) > 0;
}
std::vector<EncoderInfo> encoders() {
    std::vector<EncoderInfo> result;
    // Expose software encoders that work with the core's general-purpose filter graph.
    const std::vector<std::string> candidates{"mpeg4","mpeg2video","libx264","libx265","libvpx-vp9",
        "libaom-av1","libsvtav1","aac","libmp3lame","libopus","libvorbis","pcm_s16le","pcm_s24le","flac","mp2"};
    for (const auto& name : candidates) {
        const auto* c = avcodec_find_encoder_by_name(name.c_str());
        if (c) result.push_back({name, c->long_name ? c->long_name : name,
            c->type == AVMEDIA_TYPE_VIDEO ? "video" : "audio"});
    }
    return result;
}

void validate(const Options& o) {
    preset(o.preset);
    if (o.input.empty() || o.output.empty()) throw Error("Input and output paths are required");
    auto range = [](double v, double low, double high) { return std::isfinite(v) && v >= low && v <= high; };
    if (!range(o.start, 0, 1e9) || !range(o.duration, 0, 1e9)) throw Error("Invalid start or duration");
    for (int v : {o.width, o.height, o.cropWidth, o.cropHeight})
        if (v < 0 || v > 16384 || (v && (v < 2 || v % 2))) throw Error("Dimensions must be zero or even values in [2,16384]");
    if ((o.cropWidth == 0) != (o.cropHeight == 0) || o.cropX < 0 || o.cropY < 0 || o.cropX > 16384 || o.cropY > 16384)
        throw Error("Crop requires both width and height and nonnegative offsets");
    if (o.fit != "fit" && o.fit != "fill" && o.fit != "stretch") throw Error("Unknown resize mode");
    if (!range(o.speed, 0.25, 4) || !range(o.fps, 0, 240)) throw Error("Speed must be 0.25-4; FPS must be 0-240");
    if (o.rotation != 0 && o.rotation != 90 && o.rotation != 180 && o.rotation != 270) throw Error("Rotation must be 0/90/180/270");
    if (o.videoKbps < 1 || o.videoKbps > 1000000 || o.audioKbps < 8 || o.audioKbps > 1536 ||
        o.threads < 1 || o.threads > 32 || o.gop < 1 || o.gop > 600) throw Error("Invalid bitrate, thread count or GOP");
    if (!range(o.brightness,-1,1) || !range(o.contrast,0.1,3) || !range(o.saturation,0,3) ||
        !range(o.volumeDb,-60,24) || !range(o.loudness,-40,-5) || !range(o.fadeIn,0,3600) || !range(o.fadeOut,0,3600))
        throw Error("Invalid color, volume, loudness or fade parameters");
    if ((o.sampleRate && (o.sampleRate < 8000 || o.sampleRate > 192000)) || o.channels < 0 || o.channels > 2 ||
        o.videoStream < -1 || o.audioStream < -1 || (o.disableVideo && o.disableAudio)) throw Error("Invalid stream/audio configuration");
    if (o.crf < -1 || o.crf > 51) throw Error("CRF must be -1 or 0-51");
    const std::vector<std::string> presets{"ultrafast","superfast","veryfast","faster","fast","medium","slow","slower","veryslow"};
    if (std::find(presets.begin(), presets.end(), o.encoderPreset) == presets.end()) throw Error("Invalid encoder preset");
    if (o.mode == Mode::Remux && (o.start || o.duration || o.width || o.height || !o.videoEncoder.empty() ||
        !o.audioEncoder.empty() || o.fps || o.speed != 1 || o.rotation || o.flipHorizontal || o.flipVertical ||
        o.deinterlace || o.cropWidth || o.cropHeight || o.sampleRate || o.normalizeAudio || o.volumeDb ||
        o.fadeIn || o.fadeOut || o.brightness || o.contrast != 1 || o.saturation != 1 || o.crf >= 0))
        throw Error("Remux cannot apply encoding or filter parameters; use transcode");
    if (o.copySubtitles && o.mode != Mode::Remux) throw Error("Subtitle copying is available in remux mode");
    std::error_code ec;
    auto input = fs::absolute(fs::u8path(o.input), ec).lexically_normal();
    if (ec) throw Error("Cannot resolve input: " + ec.message());
    auto output = fs::absolute(fs::u8path(o.output), ec).lexically_normal();
    if (ec) throw Error("Cannot resolve output: " + ec.message());
    if (input == output || (fs::exists(input) && fs::exists(output) && fs::equivalent(input, output, ec)))
        throw Error("Input and output must be different files");
    if (ec) throw Error("Cannot compare files: " + ec.message());
}

MediaInfo probe(const std::string& path, const std::atomic_bool* cancel) {
    Input input(path, cancel);
    MediaInfo info;
    info.format = input.ctx->iformat->name;
    if (input.ctx->duration != AV_NOPTS_VALUE) info.duration = std::max(0.0, input.ctx->duration / double(AV_TIME_BASE));
    for (unsigned i = 0; i < input.ctx->nb_streams; ++i) {
        const auto* p = input.ctx->streams[i]->codecpar;
        const char* type = av_get_media_type_string(p->codec_type);
        info.streams.push_back({int(i), type ? type : "unknown", avcodec_get_name(p->codec_id),
            p->width, p->height, p->sample_rate, p->ch_layout.nb_channels});
    }
    return info;
}

bool remuxCompatible(const MediaInfo& info, const Options& options) {
    const auto& format = preset(options.preset);
    const auto* muxer = av_guess_format(format.container.c_str(), nullptr, nullptr);
    if (!muxer) return false;
    bool selected = false;
    for (const auto& stream : info.streams) {
        const bool audio = stream.type == "audio" && !options.disableAudio &&
                           (options.audioStream < 0 || stream.index == options.audioStream);
        const bool video = stream.type == "video" && !format.audioOnly && !options.disableVideo &&
                           (options.videoStream < 0 || stream.index == options.videoStream);
        const bool subtitle = stream.type == "subtitle" && options.copySubtitles && !format.audioOnly;
        if (!audio && !video && !subtitle) continue;
        selected = true;
        const auto* codec = avcodec_descriptor_get_by_name(stream.codec.c_str());
        if (!codec || avformat_query_codec(muxer, codec->id, FF_COMPLIANCE_NORMAL) == 0) return false;
    }
    return selected;
}

void convert(const Options& options, std::atomic_bool& cancel, const std::function<void(const Progress&)>& report) {
    cancelled(&cancel);
    validate(options);
    const auto& format = preset(options.preset);
    Input input(options.input, &cancel);
    const int64_t origin = input.ctx->start_time == AV_NOPTS_VALUE ? 0 : input.ctx->start_time;
    double total = input.ctx->duration == AV_NOPTS_VALUE ? 0 : input.ctx->duration / double(AV_TIME_BASE);
    if (total > 0 && options.start >= total) throw Error("Start time is outside the input duration");
    total = total > 0 ? std::max(0.0, total - options.start) : 0;
    if (options.duration > 0) total = total > 0 ? std::min(total, options.duration) : options.duration;

    StagedFile staged(options);
    Output output;
    check(avformat_alloc_output_context2(&output.ctx, nullptr, format.container.c_str(), staged.path().c_str()), "Create output context");
    if (!output.ctx) throw Error("Cannot allocate output context");
    output.ctx->interrupt_callback = {interrupt, &cancel};
    if (options.copyMetadata) av_dict_copy(&output.ctx->metadata, input.ctx->metadata, 0);
    if (!options.title.empty()) av_dict_set(&output.ctx->metadata, "title", options.title.c_str(), 0);
    if (!options.artist.empty()) av_dict_set(&output.ctx->metadata, "artist", options.artist.c_str(), 0);
    std::vector<int> streamMap(input.ctx->nb_streams, -1);
    std::vector<std::unique_ptr<Pipeline>> pipelines(input.ctx->nb_streams);
    int selected = 0;
    if (options.mode == Mode::Remux) {
        for (unsigned i = 0; i < input.ctx->nb_streams; ++i) {
            auto* source = input.ctx->streams[i];
            const auto type = source->codecpar->codec_type;
            bool audio = type == AVMEDIA_TYPE_AUDIO && !options.disableAudio && (options.audioStream < 0 || int(i) == options.audioStream);
            bool video = type == AVMEDIA_TYPE_VIDEO && !format.audioOnly && !options.disableVideo && (options.videoStream < 0 || int(i) == options.videoStream);
            bool subtitle = type == AVMEDIA_TYPE_SUBTITLE && options.copySubtitles && !format.audioOnly;
            if (!audio && !video && !subtitle) continue;
            if (avformat_query_codec(output.ctx->oformat, source->codecpar->codec_id, FF_COMPLIANCE_NORMAL) == 0)
                throw Error("Input codec is incompatible with the output container; use transcode");
            auto* target = avformat_new_stream(output.ctx, nullptr);
            if (!target) throw Error("Cannot allocate remux stream");
            check(avcodec_parameters_copy(target->codecpar, source->codecpar), "Copy stream parameters");
            target->codecpar->codec_tag = 0;
            target->time_base = source->time_base;
            target->avg_frame_rate = source->avg_frame_rate;
            target->sample_aspect_ratio = source->sample_aspect_ratio;
            target->disposition = source->disposition;
            if (options.copyMetadata) av_dict_copy(&target->metadata, source->metadata, 0);
            streamMap[i] = target->index;
            ++selected;
        }
    } else {
        bool video = false, audio = false;
        for (unsigned i = 0; i < input.ctx->nb_streams; ++i) {
            auto* stream = input.ctx->streams[i];
            bool chooseVideo = !video && !format.audioOnly && !options.disableVideo && (options.videoStream < 0 || int(i) == options.videoStream) && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
                && !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC);
            bool chooseAudio = !audio && !options.disableAudio && (options.audioStream < 0 || int(i) == options.audioStream) && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
            if (!chooseVideo && !chooseAudio) continue;
            pipelines[i] = configurePipeline(input.ctx, output.ctx, int(i), options, format);
            streamMap[i] = pipelines[i]->output->index;
            video |= chooseVideo;
            audio |= chooseAudio;
            ++selected;
        }
    }
    if (!selected) throw Error("No compatible audio/video stream found");
    for (const auto pair : {std::pair<int, AVMediaType>{options.videoStream, AVMEDIA_TYPE_VIDEO},
                           std::pair<int, AVMediaType>{options.audioStream, AVMEDIA_TYPE_AUDIO}}) {
        if (pair.first >= 0 && (size_t(pair.first) >= streamMap.size() || streamMap[pair.first] < 0 ||
            input.ctx->streams[pair.first]->codecpar->codec_type != pair.second)) throw Error("Selected stream index is unavailable");
    }
    if (options.start > 0) {
        check(av_seek_frame(input.ctx, -1, origin + int64_t(options.start * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD), "Seek input");
        for (auto& p : pipelines) if (p) avcodec_flush_buffers(p->decoder);
    }
    check(avio_open2(&output.ctx->pb, staged.path().c_str(), AVIO_FLAG_WRITE, &output.ctx->interrupt_callback, nullptr), "Open output");
    AVDictionary* muxOptions = nullptr;
    if (format.container == "mp4" || format.container == "ipod") av_dict_set(&muxOptions, "movflags", "+faststart", 0);
    int ret = avformat_write_header(output.ctx, &muxOptions);
    av_dict_free(&muxOptions);
    check(ret, "Write output header");
    auto packet = makePacket();
    int64_t written = 0;
    double current = 0;
    auto lastReport = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    if (report) report({0, total, 0});
    while (true) {
        cancelled(&cancel);
        ret = av_read_frame(input.ctx, packet.get());
        cancelled(&cancel);
        if (ret == AVERROR_EOF) break;
        check(ret, "Read input packet");
        int index = packet->stream_index;
        if (index < 0 || size_t(index) >= streamMap.size() || streamMap[index] < 0) {
            av_packet_unref(packet.get());
            continue;
        }
        auto* stream = input.ctx->streams[index];
        int64_t timestamp = packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts;
        if (timestamp != AV_NOPTS_VALUE)
            current = std::max(current, timestamp * av_q2d(stream->time_base) - origin / double(AV_TIME_BASE) - options.start);
        if (options.mode == Mode::Remux) {
            // Keep relative A/V offsets, normalizing all streams against one common origin.
            auto shift = av_rescale_q(origin, microseconds, stream->time_base);
            if (packet->pts != AV_NOPTS_VALUE) packet->pts -= shift;
            if (packet->dts != AV_NOPTS_VALUE) packet->dts -= shift;
            auto* target = output.ctx->streams[streamMap[index]];
            av_packet_rescale_ts(packet.get(), stream->time_base, target->time_base);
            packet->stream_index = target->index;
            packet->pos = -1;
            check(av_interleaved_write_frame(output.ctx, packet.get()), "Remux packet");
            ++written;
        } else if (!pipelines[index]->pastEnd) {
            decode(*pipelines[index], packet.get(), output.ctx, options, origin, cancel, written);
        }
        av_packet_unref(packet.get());
        auto now = std::chrono::steady_clock::now();
        if (report && now - lastReport >= std::chrono::milliseconds(150)) {
            report({current, total, total > 0 ? std::clamp(current / total, 0.0, 0.99) : 0});
            lastReport = now;
        }
        if (options.duration > 0) {
            bool done = true;
            for (const auto& p : pipelines) if (p && !p->pastEnd) done = false;
            if (done) break;
        }
    }
    for (auto& p : pipelines) {
        if (!p) continue;
        decode(*p, nullptr, output.ctx, options, origin, cancel, written);
        int flushed = av_buffersrc_add_frame_flags(p->source, nullptr, 0);
        if (flushed != AVERROR_EOF) check(flushed, "Flush filter input");
        drainFilter(*p, output.ctx, cancel, written);
        encode(*p, nullptr, output.ctx, cancel, written);
    }
    cancelled(&cancel);
    if (!written) throw Error("Selected time range contains no media frames");
    check(av_write_trailer(output.ctx), "Write output trailer");
    check(avio_closep(&output.ctx->pb), "Close output");
    cancelled(&cancel);
    staged.commit(options.overwrite);
    if (report) report({total > 0 ? total : current, total, 1});
}
} // namespace mediabridge
