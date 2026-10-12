"""Package source and a previously installed Windows runtime; no SDKs are bundled."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import zipfile

project = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument("--runtime", type=Path, default=project / "dist/windows")
p.add_argument("--output", type=Path, default=project / "dist")
args = p.parse_args()
runtime = args.runtime.resolve()
output = args.output.resolve()
if not (runtime / "bin/MediaBridge.exe").is_file():
    raise SystemExit("Install the Windows GUI runtime before packaging")
output.mkdir(parents=True, exist_ok=True)
version = re.search(r"project\(MediaBridge VERSION ([0-9.]+)", (project / "CMakeLists.txt").read_text(encoding="utf8")).group(1)
source = output / f"MediaBridge-{version}-source.zip"
binary = output / f"MediaBridge-{version}-windows-x64.zip"
top = ["CMakeLists.txt","CMakePresets.json","README.md","CHANGELOG.md","LICENSE","THIRD-PARTY-NOTICES.md",".gitignore"]
folders = ["android","include","src","qml","cmake","scripts","tests","docs","licenses"]
files = [project / name for name in top]
for folder in folders:
    files.extend(path for path in (project / folder).rglob("*") if path.is_file()
                 and "__pycache__" not in path.parts and path.suffix not in {".pyc",".pyo"})
with zipfile.ZipFile(source,"w",zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for path in sorted(files):
        if path.is_file(): archive.write(path,Path("MediaBridge")/path.relative_to(project))
    count_source = len(archive.infolist())
with zipfile.ZipFile(source) as archive:
    if archive.testzip(): raise RuntimeError("Source archive CRC failed")
shutil.copy2(source,runtime / "share/mediabridge" / source.name)
print("Source package verified",source.stat().st_size,"bytes",flush=True)
with zipfile.ZipFile(binary,"w",zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for path in sorted(runtime.rglob("*")):
        if path.is_file(): archive.write(path,Path(f"MediaBridge-{version}-windows-x64")/path.relative_to(runtime))
    count_binary = len(archive.infolist())
print("Windows archive written; checking CRC",flush=True)
with zipfile.ZipFile(binary) as archive:
    if archive.testzip(): raise RuntimeError("Windows archive CRC failed")
def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda:file.read(1024*1024),b""): h.update(block)
    return h.hexdigest()
manifest = {"version":version,"artifacts":[
    {"file":path.name,"size":path.stat().st_size,"sha256":digest(path),"entries":count}
    for path,count in ((source,count_source),(binary,count_binary))]}
for name in (f"MediaBridge-{version}-android.apk", f"MediaBridge-{version}-android-codec-source.zip"):
    path = output / name
    if path.is_file():
        with zipfile.ZipFile(path) as archive:
            if archive.testzip(): raise RuntimeError(f"Archive CRC failed: {name}")
            count = len(archive.infolist())
        manifest["artifacts"].append({"file":name,"size":path.stat().st_size,"sha256":digest(path),"entries":count})
(output / f"MediaBridge-{version}-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf8")
(output / f"MediaBridge-{version}.sha256").write_text("".join(item["sha256"]+"  "+item["file"]+"\n" for item in manifest["artifacts"]),encoding="ascii")
print(json.dumps(manifest,indent=2),flush=True)
