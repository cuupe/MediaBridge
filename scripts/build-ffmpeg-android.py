"""Build an Android FFmpeg SDK on Windows with Git Bash, GNU Make and the NDK.
External codec SDKs are supplied in deps/ffmpeg-codecs-android/<ABI>.
"""
import argparse,os,re,subprocess,shutil
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--abi',required=True,choices=['arm64-v8a','x86_64'])
p.add_argument('--source',type=Path)
p.add_argument('--ndk',type=Path)
p.add_argument('--bash',default='D:/Git/bin/bash.exe' if Path('D:/Git/bin/bash.exe').exists() else shutil.which('bash'))
p.add_argument('--make',type=Path)
p.add_argument('--pkg-config',type=Path)
p.add_argument('--jobs',default='4')
p.add_argument('--reuse-config',action='store_true')
a=p.parse_args()
root=Path(__file__).resolve().parents[1];deps=root/'deps'
source=(a.source or deps/'ffmpeg-7.1.1').resolve()
ndk=(a.ndk or deps/'android-ndk-r27c').resolve()
make=(a.make or deps/'msys-tools/usr/bin/make.exe').resolve()
pkg=(a.pkg_config or deps/'pkgconf/mingw64/bin/pkgconf.exe').resolve()
abi=a.abi;arch,triple=('aarch64','aarch64-linux-android') if abi=='arm64-v8a' else ('x86_64','x86_64-linux-android')
tool=ndk/'toolchains/llvm/prebuilt/windows-x86_64'
codecs=deps/'ffmpeg-codecs-android'/abi
prefix=deps/'ffmpeg-android'/abi
build=root/f'build-ffmpeg-{abi}';build.mkdir(parents=True,exist_ok=True)
env={k.upper():v for k,v in os.environ.items()}
shell=(Path(a.bash).resolve().parent.parent/'usr/bin/sh.exe').as_posix()
env['PATH']=(Path(a.bash).resolve().parent.parent/'usr/bin').as_posix()+';'+env.get('PATH','')
env['PKG_CONFIG_PATH']=(codecs/'lib/pkgconfig').as_posix()
# Windows CreateProcess has a command length limit. GNU Make writes linker objects to a response file.
library=source/'ffbuild/library.mak'
original=library.read_text(encoding='utf8');s=original
s=s.replace('$$(M) $(SRC_PATH)/ffbuild/pkgconfig_generate.sh','$$(M) $$(SHELL) $(SRC_PATH)/ffbuild/pkgconfig_generate.sh')
s=s.replace('$$(M) $$(SRC_PATH)/ffbuild/libversion.sh','$$(M) $$(SHELL) $$(SRC_PATH)/ffbuild/libversion.sh')
old='\t$$(LD) $(SHFLAGS) $(LDFLAGS) $(LDSOFLAGS) $$(LD_O) $$(filter %.o,$$^) $(FFEXTRALIBS)'
s=s.replace(old,'\t$$(file >$$@.rsp,$$(filter %.o,$$^))\n\t$$(LD) $(SHFLAGS) $(LDFLAGS) $(LDSOFLAGS) $$(LD_O) @$$@.rsp $(FFEXTRALIBS)')
if s!=original:library.write_text(s,encoding='utf8',newline='\n')
file=source/'Makefile';s=file.read_text(encoding='utf8');s=s.replace('VERSION_SH  = $(SRC_PATH)/ffbuild/version.sh','VERSION_SH  = $(SHELL) $(SRC_PATH)/ffbuild/version.sh');file.write_text(s,encoding='utf8',newline='\n')
cmd=[a.bash,(source/'configure').as_posix(),f'--prefix={prefix.as_posix()}','--target-os=android',f'--arch={arch}','--enable-cross-compile','--enable-shared','--disable-static','--enable-pic',f'--cc={tool.as_posix()}/bin/clang.exe --target={triple}28',f'--cxx={tool.as_posix()}/bin/clang++.exe --target={triple}28',*[f'--{flag}={tool.as_posix()}/bin/llvm-{exe}.exe' for flag,exe in [('ar','ar'),('ranlib','ranlib'),('strip','strip'),('nm','nm')]],f'--sysroot={tool.as_posix()}/sysroot','--disable-autodetect','--disable-programs','--disable-doc','--disable-debug','--disable-network','--disable-x86asm','--disable-avdevice','--disable-symver','--enable-gpl','--enable-version3','--enable-libx264','--enable-libmp3lame','--enable-libopus','--enable-libvorbis','--enable-libvpx',f'--pkg-config={pkg.as_posix()}','--pkg-config-flags=--static','--extra-libs=-lm',f'--extra-cflags=-fPIC -I{codecs.as_posix()}/include',f'--extra-ldflags=-L{codecs.as_posix()}/lib -Wl,-z,max-page-size=16384']
print('CONFIGURE FFMPEG',abi,flush=True)
if not a.reuse_config: subprocess.run(cmd,cwd=build,env=env,check=True)
for filename in ('Makefile','ffbuild/config.mak','ffbuild/config.sh'):
    file=build/filename;s=file.read_text(encoding="utf8");s=re.sub(r'/([a-z])/',lambda m:m[1].upper()+':/',s)
    if filename=='ffbuild/config.mak': s=re.sub(r'-Wl,-soname,\S+',r'-Wl,-soname,$(SLIBNAME)',s)
    file.write_text(s,encoding='utf8',newline='\n')
for target in ('all','install'):
    print('MAKE FFMPEG',abi,target,flush=True)
    subprocess.run([make.as_posix(),f'-j{a.jobs}',f'SHELL={shell}',target],cwd=build,env=env,check=True)
print('FFMPEG COMPLETE',abi,flush=True)
