"""GUI rendering plus native drop/mouse/keyboard integration checks."""
import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("--exe", required=True)
p.add_argument("--qt", required=True)
p.add_argument("--ffmpeg", required=True)
p.add_argument("--ffprobe", required=True)
p.add_argument("--artifact-dir")
args = p.parse_args()
passed = 0
exe = Path(args.exe).resolve()
qt = Path(args.qt).resolve()
env = {k.upper() if os.name == "nt" else k: v for k, v in os.environ.items()}
env["PATH"] = str(qt / "bin") + os.pathsep + env.get("PATH", "")
if (qt / "plugins").is_dir(): env["QT_PLUGIN_PATH"] = str(qt / "plugins")
if (qt / "qml").is_dir(): env["QML_IMPORT_PATH"] = str(qt / "qml")
env["QT_QPA_PLATFORM"] = "offscreen"
env["QT_QUICK_BACKEND"] = "software"
if os.name == "nt":
    env["QT_QPA_FONTDIR"] = "C:/Windows/Fonts"
if args.artifact_dir:
    Path(args.artifact_dir).mkdir(parents=True,exist_ok=True)
    env["MB_UI_FAILURE_IMAGE"]=str(Path(args.artifact_dir)/"failure.png")

def run(command, timeout=60):
    r = subprocess.run([str(v) for v in command], env=env, capture_output=True, timeout=timeout)
    stderr = r.stderr.decode("utf8", errors="replace")
    if r.returncode or any(v in stderr for v in ("ReferenceError:", "TypeError:", "Unable to assign", "Binding loop", "is not defined", "Cannot assign", "QQmlApplicationEngine failed")):
        raise AssertionError(f"exit {r.returncode}\n{stderr}")
    return r

with tempfile.TemporaryDirectory(prefix="mediabridge-ui-") as tmp:
    root = Path(tmp)
    source = root / "preview.mp4"
    run([args.ffmpeg,"-v","error","-f","lavfi","-i","testsrc2=size=320x180:rate=24",
         "-f","lavfi","-i","sine=sample_rate=44100","-t","3","-c:v","mpeg4","-c:a","aac",source])
    for suffix, options in (("desktop", []), ("mobile", ["--compact"])):
        data_dir = root / ("ui-data-" + suffix)
        env["MEDIABRIDGE_DATA_DIR"] = str(data_dir)
        r = run([exe,*options,"--ui-test",source])
        print(suffix, r.stdout.decode("utf8",errors="replace"))
        passed += int(re.search(r"(\d+) UI checks passed", r.stdout.decode("utf8",errors="replace")).group(1))
        tasks = json.loads((data_dir/"queue.json").read_text("utf8"))["tasks"]
        from urllib.parse import urlparse, unquote
        pathname = unquote(urlparse(tasks[0]["output"]).path)
        if os.name == "nt": pathname = pathname.lstrip("/")
        output = Path(pathname)
        run([args.ffmpeg,"-v","error","-xerror","-i",output,"-f","null","-"])
        info = json.loads(run([args.ffprobe,"-v","error","-show_streams","-of","json",output]).stdout)
        assert info["streams"][0]["width"] == 160
        print("PASS",suffix,"GUI conversion decodes at configured size")
        passed += 1
    wav=root/"pcm.wav"
    run([args.ffmpeg,"-v","error","-f","lavfi","-i","sine=sample_rate=44100","-t","191","-c:a","pcm_s16le",wav])
    for suffix, options in (("desktop",[]),("mobile",["--compact"])):
        data_dir=root/("audio-data-"+suffix);env["MEDIABRIDGE_DATA_DIR"]=str(data_dir)
        r=run([exe,*options,"--ui-test-audio",wav])
        print(suffix,r.stdout.decode("utf8",errors="replace"))
        passed+=int(re.search(r"(\d+) UI checks passed",r.stdout.decode("utf8",errors="replace")).group(1))
        task=json.loads((data_dir/"queue.json").read_text("utf8"))["tasks"][0]
        pathname=unquote(urlparse(task["output"]).path)
        output=Path(pathname.lstrip("/") if os.name=="nt" else pathname)
        run([args.ffmpeg,"-v","error","-xerror","-i",output,"-f","null","-"])
        info=json.loads(run([args.ffprobe,"-v","error","-show_streams","-show_format","-of","json",output]).stdout)
        assert info["streams"][0]["codec_name"]=="mp3" and abs(float(info["format"]["duration"])-191)<0.1
        print("PASS",suffix,"Japanese 3:11 WAV to MP3 independently decodes with full duration")
        passed+=1
    artifacts = Path(args.artifact_dir) if args.artifact_dir else root / "screenshots"
    artifacts.mkdir(parents=True,exist_ok=True)
    env["MEDIABRIDGE_DATA_DIR"]=str(root/"layout-data")
    r=run([exe,"--ui-test-layout",source,artifacts],timeout=90)
    layout_output=r.stdout.decode("utf8",errors="replace")
    count=int(re.search(r"(\d+) layout checks passed",layout_output).group(1))
    passed+=count
    print(f"PASS adaptive layout: {count} geometry, navigation and keyboard checks across 17 screen shapes")
    env["MEDIABRIDGE_DATA_DIR"] = str(root / "preview-data")
    preview = root / "preview-data"
    preview.mkdir()
    first = {"id":"real-fixture-1","name":"preview.mp4","input":source.as_uri(),"state":"queued","profile":{"format":"mp4","width":1280},"selected":True}
    second = dict(first,id="real-fixture-2",name="audio.flac",profile={"format":"flac"})
    (preview/"queue.json").write_text(json.dumps({"version":2,"tasks":[first,second]}),encoding="utf8")
    cases = [
        ("desktop-queue",[]),
        ("desktop-video",["--page","1"]),
        ("desktop-audio",["--page","1","--section","1"]),
        ("desktop-time",["--page","1","--section","2"]),
        ("desktop-metadata",["--page","1","--section","3"]),
        ("desktop-settings",["--page","2"]),
        ("mobile-queue",["--compact"]),
        ("mobile-processing",["--compact","--page","1"]),
        ("mobile-settings",["--compact","--page","2"]),
    ]
    for name, options in cases:
        path = artifacts / (name+".png")
        run([exe,*options,"--screenshot",path])
        assert path.exists() and path.stat().st_size > 3000
        print("PASS",name)
        passed += 1
    (preview/"settings.ini").write_text("[settings]\ntheme=light\n",encoding="utf8")
    for name, options in (("desktop-light",[]),("mobile-light",["--compact","--page","2"])):
        path=artifacts/(name+".png")
        run([exe,*options,"--screenshot",path])
        assert path.exists() and path.stat().st_size>3000
        print("PASS",name)
        passed += 1
print(f"{passed} GUI checks passed")
