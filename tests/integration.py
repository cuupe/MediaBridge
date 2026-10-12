"""Real FFmpeg encode/decode and output-safety regression checks (stdlib only)."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("--cli", required=True)
p.add_argument("--ffmpeg", required=True)
p.add_argument("--ffprobe", required=True)
p.add_argument("--checks", required=True)
args = p.parse_args()
passed = []

def run(command, ok=True):
    result = subprocess.run([str(x) for x in command], capture_output=True, timeout=90)
    if ok and result.returncode:
        raise AssertionError(result.stderr.decode("utf-8", errors="replace"))
    if not ok and result.returncode == 0:
        raise AssertionError("Expected failure: " + repr(command))
    return result

def inspect(path):
    result = run([args.ffprobe, "-v", "error", "-show_streams", "-show_format", "-of", "json", path])
    return json.loads(result.stdout)

def decode(path):
    run([args.ffmpeg, "-v", "error", "-xerror", "-i", path, "-f", "null", "-"])

def convert(source, dest, *options, ok=True):
    return run([args.cli, "convert", "-i", source, "-o", dest, *options], ok=ok)

def check(name, condition):
    if not condition:
        raise AssertionError(name)
    passed.append(name)
    print("PASS", name)

with tempfile.TemporaryDirectory(prefix="mediabridge-test-") as tmp:
    root = Path(tmp)
    source = root / "source.mp4"
    run([args.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=24",
         "-f", "lavfi", "-i", "sine=frequency=880:sample_rate=44100",
         "-t", "3", "-c:v", "mpeg4", "-bf", "2", "-c:a", "aac", source])
    metadata = json.loads(run([args.cli, "probe", source]).stdout)
    check("probe streams and duration", len(metadata["streams"]) == 2 and 2.9 < metadata["duration"] < 3.2)

    dest = root / "resized.mp4"
    convert(source, dest, "--width", "160", "--start", "0.5", "--duration", "1.25")
    info = inspect(dest)
    video = next(s for s in info["streams"] if s["codec_type"] == "video")
    decode(dest)
    check("resize, exact trim and decoder/encoder drain",
          video["width"] == 160 and video["height"] == 90 and 1.15 < float(info["format"]["duration"]) < 1.4)

    full = root / "full.mp4"
    convert(source, full)
    decode(full)
    info = inspect(full)
    video = next(s for s in info["streams"] if s["codec_type"] == "video")
    check("all delayed source frames survive flush", int(video["nb_frames"]) == 72)

    def hashes(path):
        data = json.loads(run([args.ffprobe, "-v", "error", "-show_packets", "-show_data_hash", "sha256",
                              "-show_entries", "packet=stream_index,data_hash", "-of", "json", path]).stdout)
        result = {}
        for packet in data["packets"]:
            result.setdefault(packet["stream_index"], []).append(packet["data_hash"])
        return result
    remux = root / "remux.mkv"
    convert(source, remux, "--format", "mkv", "--mode", "remux")
    decode(remux)
    check("remux preserves compressed packet payloads", hashes(source) == hashes(remux))

    wav = root / "ビートまりお - ナイト・オブ・ナイツ.wav"
    run([args.ffmpeg,"-v","error","-f","lavfi","-i","sine=sample_rate=44100","-t","3","-c:a","pcm_s16le",wav])
    audio = root / "converted.mp3"
    convert(wav,audio,"--format","mp3")
    decode(audio)
    check("Japanese PCM WAV to genuine MP3",inspect(audio)["streams"][0]["codec_name"]=="mp3")
    incompatible = root / "incompatible.mp3"
    result=convert(wav,incompatible,"--format","mp3","--mode","remux",ok=False)
    check("explicit CLI remux rejects PCM to MP3 without partial output",not incompatible.exists() and b"incompatible" in result.stderr)
    compatible = root / "copy.mp3"
    convert(audio,compatible,"--format","mp3","--mode","remux")
    decode(compatible)
    check("compatible MP3 remux preserves compressed packets",hashes(audio)==hashes(compatible))

    values = json.loads(run([args.cli, "presets"]).stdout)
    for item in values:
        if not item["available"] or item["id"] == "mp4":
            continue
        dest = root / ("audio-or-video." + item["id"])
        convert(source, dest, "--format", item["id"])
        decode(dest)
        info = inspect(dest)
        if item["id"] in {"wav", "flac", "m4a", "mp3", "ogg", "opus"}:
            check("audio extraction " + item["id"], len(info["streams"]) == 1 and info["streams"][0]["codec_type"] == "audio")
        else:
            check("video preset " + item["id"], len(info["streams"]) == 2)

    unicode_source = root / "中文 空格 & 输入.mp4"
    unicode_source.write_bytes(source.read_bytes())
    unicode_output = root / "中文 空格 & 输出.mp4"
    convert(unicode_source, unicode_output)
    decode(unicode_output)
    check("UTF-8 paths and literal shell metacharacters", unicode_output.stat().st_size > 0)

    survivor = root / "existing.mp4"
    survivor.write_bytes(b"original output")
    original = hashlib.sha256(survivor.read_bytes()).digest()
    convert(source, survivor, ok=False)
    check("default refuses overwrite", hashlib.sha256(survivor.read_bytes()).digest() == original)
    convert(source, survivor, "--overwrite", "--video-encoder", "missing_encoder", ok=False)
    check("failed overwrite retains original", hashlib.sha256(survivor.read_bytes()).digest() == original)
    run([args.checks, source, survivor, "overwrite"])
    check("in-flight cancellation retains existing output", hashlib.sha256(survivor.read_bytes()).digest() == original)
    cancelled_file = root / "cancelled.mp4"
    run([args.checks, source, cancelled_file, "new"])
    check("in-flight cancellation leaves no output", not cancelled_file.exists())
    convert(source, survivor, "--overwrite")
    decode(survivor)
    check("successful overwrite commits new output", hashlib.sha256(survivor.read_bytes()).digest() != original)

    source_hash = hashlib.sha256(source.read_bytes()).digest()
    convert(source, source, "--overwrite", ok=False)
    check("same input/output is rejected", hashlib.sha256(source.read_bytes()).digest() == source_hash)
    broken = root / "broken.mp4"
    broken.write_bytes(b"not media")
    failed = root / "failed.mp4"
    convert(broken, failed, ok=False)
    check("invalid input leaves no output", not failed.exists())
    convert(source, failed, "--start", "999", ok=False)
    check("out-of-range trim leaves no output", not failed.exists())
    convert(source, failed, "--mode", "remux", "--width", "160", ok=False)
    check("remux rejects unsupported transforms", not failed.exists())
    convert(source, failed, "--format", "webm", "--mode", "remux", ok=False)
    check("remux rejects incompatible codec/container", not failed.exists())

    delayed = root / "delayed.mkv"
    run([args.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=24",
         "-itsoffset", "0.25", "-f", "lavfi", "-i", "sine=frequency=400:sample_rate=48000",
         "-t", "2", "-c:v", "mpeg4", "-c:a", "pcm_s16le", "-output_ts_offset", "5", delayed])
    result = root / "offset.mp4"
    convert(delayed, result)
    decode(result)
    streams = inspect(result)["streams"]
    starts = {s["codec_type"]: float(s.get("start_time", 0)) for s in streams}
    check("common origin preserves relative A/V delay", 0.20 < starts["audio"] - starts["video"] < 0.28)

    mono = root / "mono.wav"
    run([args.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "sine=sample_rate=48000", "-t", "0.4", mono])
    result = root / "mono.flac"
    convert(mono, result, "--format", "flac")
    decode(result)
    check("audio-only mono input resamples to stereo", inspect(result)["streams"][0]["channels"] == 2)
    silent = root / "silent.mp4"
    run([args.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=24",
         "-t", "0.5", "-c:v", "mpeg4", silent])
    result = root / "silent-out.mp4"
    convert(silent, result)
    decode(result)
    check("video-only input works", len(inspect(result)["streams"]) == 1)
    convert(silent, failed, "--format", "wav", ok=False)
    check("audio extraction rejects missing audio", not failed.exists())

    rotated = root / "rotated.mp4"
    run([args.ffmpeg, "-v", "error", "-display_rotation", "90", "-i", source, "-c", "copy", rotated])
    rotation_output = root / "rotation-output.mp4"
    convert(rotated, rotation_output)
    rotation_stream = next(s for s in inspect(rotation_output)["streams"] if s["codec_type"] == "video")
    rotations = [s.get("rotation") for s in rotation_stream.get("side_data_list", []) if "rotation" in s]
    check("phone video rotation matrix is preserved", rotations and abs(rotations[0]) == 90)

    vfr = root / "vfr.mkv"
    run([args.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=24",
         "-t", "2", "-vf", "select=not(mod(n\\,3))", "-fps_mode", "vfr", "-c:v", "mpeg4", vfr])
    vfr_output = root / "vfr-output.mp4"
    convert(vfr, vfr_output)
    decode(vfr_output)
    count = int(inspect(vfr_output)["streams"][0]["nb_frames"])
    check("variable frame rate preserves selected frames", count == 16)

    multiple = root / "multiple.mkv"
    run([args.ffmpeg, "-v", "error", "-i", source, "-map", "0:v", "-map", "0:a", "-map", "0:a",
         "-c", "copy", multiple])
    multiple_output = root / "multiple-output.mkv"
    convert(multiple, multiple_output, "--format", "mkv", "--mode", "remux")
    check("remux retains multiple audio tracks", len(inspect(multiple_output)["streams"]) == 3)
    check("temporary output directories are cleaned", not list(root.glob(".mediabridge-*")))

    shaped = root / "shaped.mp4"
    convert(source, shaped, "--crop-width", "160", "--crop-height", "100", "--crop-x", "40", "--crop-y", "20",
            "--rotation", "90", "--width", "180", "--height", "180", "--fit", "fit", "--fps", "12")
    decode(shaped)
    v = next(s for s in inspect(shaped)["streams"] if s["codec_type"] == "video")
    check("crop, physical rotation, fit padding and frame rate", v["width"] == 180 and v["height"] == 180 and int(v["nb_frames"]) == 36)
    filled = root / "fill.mp4"
    convert(source, filled, "--width", "160", "--height", "160", "--fit", "fill", "--flip-horizontal", "--flip-vertical",
            "--brightness", "0.05", "--contrast", "1.2", "--saturation", "0.8", "--deinterlace")
    decode(filled)
    v = inspect(filled)["streams"][0]
    check("fill, flips, color correction and deinterlace", v["width"] == 160 and v["height"] == 160)
    stretched = root / "stretch.mp4"
    convert(source, stretched, "--width", "160", "--height", "160", "--fit", "stretch", "--no-audio")
    decode(stretched)
    check("stretch and audio stream removal", len(inspect(stretched)["streams"]) == 1)
    automatic = root / "auto-rotation.mp4"
    convert(rotated, automatic, "--auto-rotate")
    decode(automatic)
    v = inspect(automatic)["streams"][0]
    check("phone rotation applied to pixels without double rotation", v["width"] == 180 and v["height"] == 320
          and not any(s.get("rotation", 0) for s in v.get("side_data_list", [])))

    for speed in (0.25, 2, 4):
        fast = root / ("speed-" + str(speed) + ".mp4")
        convert(source, fast, "--speed", str(speed))
        decode(fast)
        data = inspect(fast)
        check("audio/video speed " + str(speed), abs(float(data["format"]["duration"]) - 3/speed) < 0.25 and len(data["streams"]) == 2)
    sampled = root / "sampled.flac"
    convert(source, sampled, "--format", "flac", "--sample-rate", "16000", "--channels", "1", "--audio-kbps", "64")
    decode(sampled)
    a = inspect(sampled)["streams"][0]
    check("explicit sample rate and mono downmix", int(a["sample_rate"]) == 16000 and a["channels"] == 1)

    import array, math
    def samples(path, start=0.3, duration=0.5):
        data = run([args.ffmpeg, "-v", "error", "-ss", str(start), "-i", path, "-t", str(duration),
                    "-vn", "-ac", "1", "-ar", "16000", "-f", "s16le", "-"]).stdout
        values = array.array("h"); values.frombytes(data)
        return values
    def rms(values):
        return math.sqrt(sum(float(v)*v for v in values)/max(1, len(values)))
    quiet = root / "quiet.flac"
    convert(source, quiet, "--format", "flac", "--channels", "1", "--sample-rate", "16000", "--volume-db", "-6")
    check("audio gain changes measured PCM amplitude", 0.47 < rms(samples(quiet))/rms(samples(sampled)) < 0.53)
    normalized = root / "normalized.flac"
    convert(source, normalized, "--format", "flac", "--normalize-audio", "--loudness", "-16")
    result = run([args.ffmpeg, "-v", "info", "-i", normalized, "-af", "loudnorm=I=-16:print_format=json", "-f", "null", "-"])
    stderr = result.stderr.decode("utf8", errors="replace")
    meter = json.loads(stderr[stderr.rfind("{"):stderr.rfind("}")+1])
    check("loudness normalization reaches target", abs(float(meter["input_i"]) + 16) < 1.5)
    faded = root / "faded.mp4"
    convert(source, faded, "--fade-in", "0.6", "--fade-out", "0.6")
    decode(faded)
    check("audio fade-in and fade-out envelope", rms(samples(faded,0,0.06)) < rms(samples(faded,1,0.2))*0.25
          and rms(samples(faded,2.85,0.08)) < rms(samples(faded,1,0.2))*0.4)

    tags = root / "tags.mp4"
    convert(source, tags, "--no-metadata", "--title", "MediaBridge Test", "--artist", "Queue Team")
    data = inspect(tags)["format"].get("tags", {})
    check("metadata overrides survive mux", data.get("title") == "MediaBridge Test" and data.get("artist") == "Queue Team")
    track = root / "track.wav"
    convert(multiple, track, "--format", "wav", "--audio-stream", "2", "--channels", "0")
    check("explicit stream selection", len(inspect(track)["streams"]) == 1)
    convert(source, failed, "--audio-stream", "0", ok=False)
    check("mismatched stream index rejected", not failed.exists())
    convert(source, failed, "--crop-width", "400", "--crop-height", "200", ok=False)
    check("out-of-bounds crop rejected atomically", not failed.exists())
    convert(source, failed, "--speed", "0", ok=False)
    check("invalid speed rejected atomically", not failed.exists())
    convert(source, failed, "--crf", "24", ok=False)
    check("unsupported encoder CRF rejected", not failed.exists())
    encoder_values = json.loads(run([args.cli, "encoders"]).stdout)
    if any(e["id"] == "libx264" for e in encoder_values):
        crf = root / "crf.mp4"
        convert(source, crf, "--video-encoder", "libx264", "--crf", "25", "--encoder-preset", "veryfast", "--threads", "2")
        decode(crf)
        check("H.264 CRF encoding", inspect(crf)["streams"][0]["codec_name"] == "h264")

    subtitle = root / "captions.srt"
    subtitle.write_text("1\n00:00:00,000 --> 00:00:02,000\nMediaBridge subtitle\n", encoding="utf8")
    subtitled = root / "subtitled.mkv"
    run([args.ffmpeg, "-v", "error", "-i", source, "-i", subtitle, "-map", "0", "-map", "1", "-c", "copy", subtitled])
    subout = root / "subout.mkv"
    convert(subtitled, subout, "--format", "mkv", "--mode", "remux", "--copy-subtitles")
    check("compatible subtitle stream copied during remux", any(s["codec_type"] == "subtitle" for s in inspect(subout)["streams"]))
    batch_in = root / "batch-input"; batch_out = root / "batch-output"
    batch_in.mkdir(); batch_out.mkdir()
    (batch_in / "one.mp4").write_bytes(source.read_bytes())
    (batch_in / "two.mp4").write_bytes(source.read_bytes())
    (batch_in / "bad.mp4").write_bytes(b"bad input")
    data = json.loads(run([args.cli,"batch","-i",batch_in,"-o",batch_out,"--width","160"],ok=False).stdout)
    check("CLI batch continues after individual failures", sum(t["state"]=="done" for t in data)==2 and sum(t["state"]=="failed" for t in data)==1)
    data = json.loads(run([args.cli,"batch","-i",batch_in / "one.mp4","-o",batch_out,"--conflict","skip"]).stdout)
    check("CLI batch skip policy", data[0]["state"]=="skipped")
    data = json.loads(run([args.cli,"batch","-i",batch_in / "one.mp4","-o",batch_out]).stdout)
    check("CLI batch rename policy", "(1)" in data[0]["output"])
    check("advanced processing cleans staging directories", not list(root.rglob(".mediabridge-*")))
print(str(len(passed)) + " integration checks passed")
