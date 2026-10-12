#pragma once
#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace mediabridge {
enum class Mode { Transcode, Remux };
struct Preset {
    std::string id, label, extension, container, videoEncoder, audioEncoder;
    bool audioOnly = false;
};
struct Options {
    std::string input, output, preset = "mp4";
    Mode mode = Mode::Transcode;
    std::string videoEncoder, audioEncoder, encoderPreset = "fast";
    double start = 0, duration = 0, fps = 0, speed = 1;
    int width = 0, height = 0;
    std::string fit = "fit";
    int videoKbps = 2500, audioKbps = 192, crf = -1, threads = 2, gop = 48;
    int rotation = 0, cropX = 0, cropY = 0, cropWidth = 0, cropHeight = 0;
    bool flipHorizontal = false, flipVertical = false, deinterlace = false, autoRotate = false;
    double brightness = 0, contrast = 1, saturation = 1;
    int sampleRate = 0, channels = 2, videoStream = -1, audioStream = -1;
    double volumeDb = 0, loudness = -16, fadeIn = 0, fadeOut = 0;
    bool normalizeAudio = false, disableVideo = false, disableAudio = false;
    bool copyMetadata = true, copySubtitles = false, overwrite = false;
    std::string title, artist;
};
struct StreamInfo {
    int index = 0;
    std::string type, codec;
    int width = 0, height = 0, sampleRate = 0, channels = 0;
};
struct MediaInfo { std::string format; double duration = 0; std::vector<StreamInfo> streams; };
struct Progress { double seconds = 0, duration = 0, fraction = 0; };
struct EncoderInfo { std::string name, label, type; };
class Error : public std::runtime_error { using std::runtime_error::runtime_error; };
class Cancelled : public Error { public: Cancelled() : Error("Operation cancelled") {} };
const std::vector<Preset>& presets();
const Preset& preset(const std::string& id);
bool encoderAvailable(const std::string& name);
bool encoderCompatible(const std::string& presetId, const std::string& name);
std::vector<EncoderInfo> encoders();
void validate(const Options& options);
MediaInfo probe(const std::string& path, const std::atomic_bool* cancel = nullptr);
bool remuxCompatible(const MediaInfo& info, const Options& options);
// Owns all contexts. Local outputs are committed only after trailer/close succeed.
void convert(const Options& options, std::atomic_bool& cancel,
             const std::function<void(const Progress&)>& progress = {});
}
