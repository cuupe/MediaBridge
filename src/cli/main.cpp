#include "mediabridge/media.hpp"
#include <csignal>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
std::atomic_bool cancelFlag{false};
static_assert(std::atomic_bool::is_always_lock_free, "Signal handler requires a lock-free atomic");
void signalHandler(int) { cancelFlag.store(true, std::memory_order_relaxed); }

std::string json(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << char(c);
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else out << char(c);
    }
    out << '"';
    return out.str();
}
double number(const std::string& value) {
    std::istringstream in(value);
    in.imbue(std::locale::classic());
    double result;
    if (!(in >> result) || !in.eof()) throw mediabridge::Error("Invalid number: " + value);
    return result;
}
int integer(const std::string& value) {
    size_t used = 0;
    int result = std::stoi(value, &used);
    if (used != value.size()) throw mediabridge::Error("Invalid integer: " + value);
    return result;
}
void help() {
    std::cout << R"(MediaBridge Studio 2.0 - FFmpeg media processor
  mediabridge-cli presets | encoders
  mediabridge-cli probe INPUT
  mediabridge-cli convert -i INPUT -o OUTPUT [options]
  mediabridge-cli batch -i FILE_OR_FOLDER [-i MORE] -o DIRECTORY [options]
Formats: mp4 mkv webm mov avi ts m4a mp3 wav flac ogg opus
  --format ID --mode transcode|remux
  --start SECONDS --duration SECONDS --speed 0.25..4
  --width EVEN_PX --height EVEN_PX --fit fit|fill|stretch --fps 0..240
  --video-encoder NAME --audio-encoder NAME --encoder-preset fast
  --video-kbps 2500 --audio-kbps 192 --crf -1..51 --threads 1..32 --gop 48
  --crop-x PX --crop-y PX --crop-width EVEN_PX --crop-height EVEN_PX
  --rotation 0|90|180|270 --auto-rotate --flip-horizontal --flip-vertical
  --deinterlace --brightness -1..1 --contrast 0.1..3 --saturation 0..3
  --sample-rate HZ --channels 0|1|2 --volume-db -60..24
  --normalize-audio --loudness -40..-5 --fade-in SEC --fade-out SEC
  --video-stream INDEX --audio-stream INDEX --no-video --no-audio
  --no-metadata --title TEXT --artist TEXT --copy-subtitles (remux only)
  --overwrite (atomic replacement only after success)
Batch: --recursive --conflict rename|skip|overwrite --suffix _converted
Batch processes sequentially and continues after individual failures.
GUI supports parallel tasks, per-task profiles and persisted settings.
Ctrl+C cancels. Exit codes: 0 success, 1 error, 2 cancelled.
Progress/errors: stderr. Media information and batch reports: JSON stdout.
)";
}} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // The CRT narrow argv can lose Chinese paths; parse the UTF-16 command line.
    int wideCount = 0;
    LPWSTR* wideArgs = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    std::vector<std::string> utf8Args;
    std::vector<char*> argumentPointers;
    if (!wideArgs) return 1;
    for (int i = 0; i < wideCount; ++i) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wideArgs[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideArgs[i], -1, s.data(), bytes, nullptr, nullptr);
        s.pop_back();
        utf8Args.push_back(std::move(s));
    }
    LocalFree(wideArgs);
    for (auto& s : utf8Args) argumentPointers.push_back(s.data());
    argc = wideCount;
    argv = argumentPointers.data();
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::cout.imbue(std::locale::classic());
    std::cerr.imbue(std::locale::classic());
    try {
        if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") { help(); return 0; }
        std::string command = argv[1];
        if (command == "presets") {
            if (argc != 2) throw mediabridge::Error("presets takes no arguments");
            std::cout << '[';
            bool first = true;
            for (const auto& p : mediabridge::presets()) {
                if (!first) std::cout << ',';
                first = false;
                bool available = mediabridge::encoderAvailable(p.videoEncoder) && mediabridge::encoderAvailable(p.audioEncoder);
                std::cout << "{\"id\":" << json(p.id) << ",\"label\":" << json(p.label)
                          << ",\"videoEncoder\":" << json(p.videoEncoder) << ",\"audioEncoder\":" << json(p.audioEncoder)
                          << ",\"available\":" << (available ? "true" : "false") << '}';
            }
            std::cout << "]\n";
            return 0;
        }
        if (command == "probe") {
            if (argc != 3) throw mediabridge::Error("Usage: mediabridge-cli probe INPUT");
            auto info = mediabridge::probe(argv[2], &cancelFlag);
            std::cout << "{\"format\":" << json(info.format) << ",\"duration\":" << info.duration << ",\"streams\":[";
            bool first = true;
            for (const auto& s : info.streams) {
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"index\":" << s.index << ",\"type\":" << json(s.type) << ",\"codec\":" << json(s.codec)
                          << ",\"width\":" << s.width << ",\"height\":" << s.height
                          << ",\"sampleRate\":" << s.sampleRate << ",\"channels\":" << s.channels << '}';
            }
            std::cout << "]}\n";
            return 0;
        }
        if (command == "encoders") {
            std::cout << '['; bool first = true;
            for (const auto& e : mediabridge::encoders()) {
                if (!first) std::cout << ','; first = false;
                std::cout << "{\"id\":" << json(e.name) << ",\"label\":" << json(e.label) << ",\"type\":" << json(e.type) << '}';
            }
            std::cout << "]\n"; return 0;
        }
        bool batch = command == "batch", recursive = false;
        std::vector<std::string> inputs;
        std::string conflict = "rename", suffix = "_converted";
        if (command != "convert" && !batch) throw mediabridge::Error("Unknown command: " + command);
        mediabridge::Options options;
        for (int i = 2; i < argc; ++i) {
            std::string flag = argv[i];
            if (flag == "--overwrite") { options.overwrite = true; conflict = "overwrite"; continue; }
            if (flag == "--recursive") { recursive = true; continue; }
            if (flag == "--flip-horizontal") { options.flipHorizontal = true; continue; }
            if (flag == "--flip-vertical") { options.flipVertical = true; continue; }
            if (flag == "--deinterlace") { options.deinterlace = true; continue; }
            if (flag == "--auto-rotate") { options.autoRotate = true; continue; }
            if (flag == "--normalize-audio") { options.normalizeAudio = true; continue; }
            if (flag == "--no-video") { options.disableVideo = true; continue; }
            if (flag == "--no-audio") { options.disableAudio = true; continue; }
            if (flag == "--no-metadata") { options.copyMetadata = false; continue; }
            if (flag == "--copy-subtitles") { options.copySubtitles = true; continue; }
            if (i + 1 >= argc) throw mediabridge::Error("Missing value for " + flag);
            std::string value = argv[++i];
            if (flag == "-i" || flag == "--input") { options.input = value; inputs.push_back(value); }
            else if (flag == "-o" || flag == "--output") options.output = value;
            else if (flag == "--format") options.preset = value;
            else if (flag == "--mode") {
                if (value == "remux") options.mode = mediabridge::Mode::Remux;
                else if (value == "transcode") options.mode = mediabridge::Mode::Transcode;
                else throw mediabridge::Error("Mode must be transcode or remux");
            } else if (flag == "--start") options.start = number(value);
            else if (flag == "--duration") options.duration = number(value);
            else if (flag == "--width") options.width = integer(value);
            else if (flag == "--video-kbps") options.videoKbps = integer(value);
            else if (flag == "--audio-kbps") options.audioKbps = integer(value);
            else if (flag == "--video-encoder") options.videoEncoder = value;
            else if (flag == "--audio-encoder") options.audioEncoder = value;
            else if (flag == "--encoder-preset") options.encoderPreset = value;
            else if (flag == "--height") options.height = integer(value);
            else if (flag == "--fit") options.fit = value;
            else if (flag == "--fps") options.fps = number(value);
            else if (flag == "--speed") options.speed = number(value);
            else if (flag == "--crf") options.crf = integer(value);
            else if (flag == "--threads") options.threads = integer(value);
            else if (flag == "--gop") options.gop = integer(value);
            else if (flag == "--rotation") options.rotation = integer(value);
            else if (flag == "--crop-x") options.cropX = integer(value);
            else if (flag == "--crop-y") options.cropY = integer(value);
            else if (flag == "--crop-width") options.cropWidth = integer(value);
            else if (flag == "--crop-height") options.cropHeight = integer(value);
            else if (flag == "--brightness") options.brightness = number(value);
            else if (flag == "--contrast") options.contrast = number(value);
            else if (flag == "--saturation") options.saturation = number(value);
            else if (flag == "--sample-rate") options.sampleRate = integer(value);
            else if (flag == "--channels") options.channels = integer(value);
            else if (flag == "--volume-db") options.volumeDb = number(value);
            else if (flag == "--loudness") options.loudness = number(value);
            else if (flag == "--fade-in") options.fadeIn = number(value);
            else if (flag == "--fade-out") options.fadeOut = number(value);
            else if (flag == "--video-stream") options.videoStream = integer(value);
            else if (flag == "--audio-stream") options.audioStream = integer(value);
            else if (flag == "--title") options.title = value;
            else if (flag == "--artist") options.artist = value;
            else if (flag == "--conflict") conflict = value;
            else if (flag == "--suffix") suffix = value;
            else throw mediabridge::Error("Unknown option: " + flag);
        }
        if (batch) {
            namespace fs = std::filesystem;
            fs::path directory = fs::u8path(options.output);
            if (!fs::is_directory(directory)) throw mediabridge::Error("Batch output must be an existing directory");
            if (inputs.empty()) throw mediabridge::Error("Batch requires at least one -i INPUT");
            if (conflict != "rename" && conflict != "skip" && conflict != "overwrite") throw mediabridge::Error("Invalid conflict policy");
            if (suffix.find_first_of("/\\<>:\"|?*") != std::string::npos) throw mediabridge::Error("Invalid output suffix");
            std::vector<fs::path> files;
            const std::vector<std::string> extensions{".mp4",".mkv",".mov",".avi",".webm",".ts",".m2ts",".mts",".mpeg",".mpg",".m4v",".3gp",
                ".mp3",".m4a",".aac",".wav",".flac",".ogg",".opus",".wma",".aiff",".mka",".vob"};
            auto add = [&](const fs::directory_entry& entry) {
                if (files.size() >= 10000) throw mediabridge::Error("Batch input limit is 10000 files");
                if (!entry.is_regular_file() || entry.is_symlink()) return;
                std::string ext = entry.path().extension().u8string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){return char(std::tolower(c));});
                if (std::find(extensions.begin(),extensions.end(),ext)!=extensions.end()) files.push_back(entry.path());
            };
            for (const auto& item : inputs) {
                fs::path path=fs::u8path(item);
                if (fs::is_directory(path)) {
                    if(recursive) for(const auto& e:fs::recursive_directory_iterator(path))add(e);
                    else for(const auto& e:fs::directory_iterator(path))add(e);
                } else files.push_back(path);
            }
            std::sort(files.begin(),files.end()); files.erase(std::unique(files.begin(),files.end()),files.end());
            if (files.empty()) throw mediabridge::Error("No media files found");
            std::string ext=mediabridge::preset(options.preset).extension;
            int errors=0; bool first=true; std::cout << "[";
            for (const auto& file:files) {
                if(cancelFlag) throw mediabridge::Cancelled();
                auto item=options; item.input=file.u8string();
                std::string base=file.stem().u8string()+suffix;
                fs::path output=directory/fs::u8path(base+"."+ext);
                if(conflict=="rename") {
                    int index=1;
                    while(fs::exists(output))output=directory/fs::u8path(base+" ("+std::to_string(index++)+")."+ext);
                }
                item.output=output.u8string(); item.overwrite=conflict=="overwrite";
                std::string state="done",error;
                if(conflict=="skip"&&fs::exists(output))state="skipped";
                else try {mediabridge::convert(item,cancelFlag);}
                catch(const mediabridge::Cancelled&) {throw;}
                catch(const std::exception& e){state="failed";error=e.what();++errors;}
                if(!first)std::cout<<',';first=false;
                std::cout<<"{\"input\":"<<json(item.input)<<",\"output\":"<<json(item.output)<<",\"state\":"<<json(state)<<",\"error\":"<<json(error)<<'}'<<std::flush;
                std::cerr<<state<<": "<<item.input<<'\n';
            }
            std::cout<<"]\n";return errors?1:0;
        }
        if (inputs.size() > 1) throw mediabridge::Error("Use batch for multiple input files");
        mediabridge::convert(options, cancelFlag, [](const mediabridge::Progress& p) {
            std::cerr << "\r" << std::fixed << std::setprecision(1) << p.seconds << 's';
            if (p.duration > 0) std::cerr << " / " << p.duration << "s (" << int(p.fraction * 100) << "%)";
            std::cerr << std::flush;
        });
        std::cerr << "\nSaved: " << options.output << '\n';
        return 0;
    } catch (const mediabridge::Cancelled&) {
        std::cerr << "\nCancelled\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << '\n';
        return 1;
    }
}
