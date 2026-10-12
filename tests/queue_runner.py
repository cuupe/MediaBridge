"""Run controller tests against a generated, genuine AV fixture."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser()
p.add_argument("--exe",required=True)
p.add_argument("--qt",required=True)
p.add_argument("--ffmpeg",required=True)
args=p.parse_args()
env={k.upper() if os.name=="nt" else k:v for k,v in os.environ.items()}
env["PATH"]=str(Path(args.qt)/"bin")+os.pathsep+env.get("PATH","")
with tempfile.TemporaryDirectory(prefix="mediabridge-queue-") as temp:
    root=Path(temp);source=root/"fixture.mp4"
    subprocess.run([args.ffmpeg,"-v","error","-f","lavfi","-i","testsrc2=size=320x180:rate=24",
        "-f","lavfi","-i","sine=sample_rate=44100","-t","3","-c:v","mpeg4","-c:a","aac",str(source)],check=True)
    wav=root/"pcm.wav"; mp3=root/"original.mp3"
    subprocess.run([args.ffmpeg,"-v","error","-f","lavfi","-i","sine=sample_rate=44100","-t","3","-c:a","pcm_s16le",str(wav)],check=True)
    subprocess.run([args.ffmpeg,"-v","error","-i",str(wav),"-c:a","libmp3lame",str(mp3)],check=True)
    subprocess.run([str(Path(args.exe).resolve()),str(source),str(root),str(wav),str(mp3)],env=env,check=True)
