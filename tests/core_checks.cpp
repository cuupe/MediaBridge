#include "mediabridge/media.hpp"
#include <filesystem>
#include <iostream>
#include <limits>

int main(int argc, char** argv) {
    try {
        if (argc == 4) {
            mediabridge::Options o;
            o.input = argv[1]; o.output = argv[2]; o.preset = "mp4";
            o.overwrite = std::string(argv[3]) == "overwrite";
            std::atomic_bool flag{false};
            bool didCancel = false;
            try {
                mediabridge::convert(o, flag, [&](const mediabridge::Progress& p) {
                    if (p.fraction < 1) flag = true;
                });
            } catch (const mediabridge::Cancelled&) { didCancel = true; }
            if (!didCancel) throw std::runtime_error("Expected cancellation during conversion");
            return 0;
        }
        mediabridge::Options o;
        o.input = "input.mp4"; o.output = "output.mp4";
        auto rejected = [&](const mediabridge::Options& value) {
            try { mediabridge::validate(value); } catch (const mediabridge::Error&) { return; }
            throw std::runtime_error("Invalid options were accepted");
        };
        o.width = 3; rejected(o);
        o.width = 0; o.start = std::numeric_limits<double>::quiet_NaN(); rejected(o);
        o.start = 0; o.duration = -1; rejected(o);
        o.duration = 0; o.mode = mediabridge::Mode::Remux; o.width = 640; rejected(o);
        o.mode = mediabridge::Mode::Transcode; o.width = 0; o.output = o.input; rejected(o);
        o.output = "output.mp4"; o.preset = "unknown"; rejected(o);
        auto expect = [](bool result,const char* name){if(!result)throw std::runtime_error(name);};
        mediabridge::MediaInfo pcm;pcm.streams={{0,"audio","pcm_s16le"}};
        o.preset="mp3";
        expect(!mediabridge::remuxCompatible(pcm,o),"PCM cannot be remuxed into MP3");
        o.preset="wav";expect(mediabridge::remuxCompatible(pcm,o),"PCM can be remuxed into WAV");
        mediabridge::MediaInfo mixed;mixed.streams={{0,"video","mpeg4"},{1,"audio","mp3"},{2,"audio","pcm_s16le"}};
        o.preset="mp3";o.audioStream=1;
        expect(mediabridge::remuxCompatible(mixed,o),"audio-only preset ignores video and unselected PCM stream");
        o.audioStream=-1;expect(!mediabridge::remuxCompatible(mixed,o),"all selected audio streams must be compatible");
        o.preset="mp4";o.disableAudio=true;
        expect(mediabridge::remuxCompatible(mixed,o),"disabled audio does not affect video remux compatibility");
        o.disableVideo=true;expect(!mediabridge::remuxCompatible(mixed,o),"empty stream selection cannot be remuxed");
        std::atomic_bool flag{true};
        try { mediabridge::convert(o, flag); throw std::runtime_error("Expected cancellation"); }
        catch (const mediabridge::Cancelled&) {}
        std::cout << "Core validation and pre-cancellation checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
