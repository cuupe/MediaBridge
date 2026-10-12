"""Verify installed Windows app without Qt SDK paths or PATH tools."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import urlparse, unquote
p=argparse.ArgumentParser()
p.add_argument("--package",required=True)
p.add_argument("--ffmpeg",required=True)
p.add_argument("--ffprobe",required=True)
args=p.parse_args()
package=Path(args.package).resolve()
env={k.upper():v for k,v in os.environ.items()}
env["PATH"]=str(Path(env.get("SYSTEMROOT","C:/Windows"))/"System32")
for name in ("QT_PLUGIN_PATH","QML_IMPORT_PATH","QML2_IMPORT_PATH"):
    env.pop(name,None)
env["QT_QPA_PLATFORM"]="offscreen"
env["QT_QUICK_BACKEND"]="software"
env["QT_QPA_FONTDIR"]=str(Path(env.get("SYSTEMROOT","C:/Windows"))/"Fonts")
def run(command):
    r=subprocess.run([str(v) for v in command],env=env,capture_output=True,timeout=60)
    error=r.stderr.decode("utf8",errors="replace")
    if r.returncode or any(s in error for s in ("ReferenceError:","TypeError:","Unable to assign","Binding loop")):
        raise AssertionError(f"exit={r.returncode}\n{error}")
    return r
with tempfile.TemporaryDirectory(prefix="mediabridge-package-") as tmp:
    root=Path(tmp);fixture=root/"fixture.mp4"
    run([args.ffmpeg,"-v","error","-f","lavfi","-i","testsrc2=size=320x180:rate=24",
         "-f","lavfi","-i","sine=sample_rate=44100","-t","3","-c:v","mpeg4","-c:a","aac",fixture])
    for name,extra in (("desktop",[]),("mobile",["--compact"])):
        data=root/name;env["MEDIABRIDGE_DATA_DIR"]=str(data)
        run([package/"bin/MediaBridge.exe",*extra,"--ui-test",fixture])
        tasks=json.loads((data/"queue.json").read_text("utf8"))["tasks"]
        output=Path(unquote(urlparse(tasks[0]["output"]).path).lstrip("/"))
        run([args.ffmpeg,"-v","error","-xerror","-i",output,"-f","null","-"])
        print("PASS packaged",name,"GUI interaction and conversion")
    wav=root/"pcm.wav"
    run([args.ffmpeg,"-v","error","-f","lavfi","-i","sine=sample_rate=44100","-t","191","-c:a","pcm_s16le",wav])
    for name,extra in (("audio-desktop",[]),("audio-mobile",["--compact"])):
        data=root/name;env["MEDIABRIDGE_DATA_DIR"]=str(data)
        run([package/"bin/MediaBridge.exe",*extra,"--ui-test-audio",wav])
        task=json.loads((data/"queue.json").read_text("utf8"))["tasks"][0]
        output=Path(unquote(urlparse(task["output"]).path).lstrip("/"))
        run([args.ffmpeg,"-v","error","-xerror","-i",output,"-f","null","-"])
        info=json.loads(run([args.ffprobe,"-v","error","-show_streams","-show_format","-of","json",output]).stdout)
        assert task["profile"]["mode"]=="transcode" and info["streams"][0]["codec_name"]=="mp3" and abs(float(info["format"]["duration"])-191)<0.1
        print("PASS packaged",name,"Japanese 3:11 WAV to MP3 conversion and independent decode")
    cli=package/"bin/mediabridge-cli.exe"
    assert len(json.loads(run([cli,"presets"]).stdout))==12
    assert json.loads(run([cli,"encoders"]).stdout)
    dest=root/"cli.mp4"
    run([cli,"convert","-i",fixture,"-o",dest,"--width","160","--height","160","--fit","fill","--normalize-audio"])
    run([args.ffmpeg,"-v","error","-xerror","-i",dest,"-f","null","-"])
    info=json.loads(run([args.ffprobe,"-v","error","-show_streams","-of","json",dest]).stdout)
    assert info["streams"][0]["width"]==160 and info["streams"][0]["height"]==160
    print("PASS packaged CLI capabilities and advanced conversion")
    out=root/"batch";out.mkdir()
    report=json.loads(run([cli,"batch","-i",fixture,"-o",out,"--format","flac"]).stdout)
    assert report[0]["state"]=="done"
    run([args.ffmpeg,"-v","error","-xerror","-i",report[0]["output"],"-f","null","-"])
    print("PASS packaged CLI batch")
print("Package verified with isolated PATH and no Qt SDK variables")
