"""Build static Android codec libraries from the versioned source folders in deps."""
import argparse,os,subprocess,shutil,re
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--abi',required=True,choices=['arm64-v8a','x86_64'])
p.add_argument('--ndk',type=Path);p.add_argument('--ninja');p.add_argument('--bash');p.add_argument('--make',type=Path)
p.add_argument('--jobs',default='4');p.add_argument('--cmake',default='cmake')
a=p.parse_args();root=Path(__file__).resolve().parents[1];deps=root/'deps';prefix=deps/'ffmpeg-codecs-android'/a.abi
ndk=(a.ndk or deps/'android-ndk-r27c').resolve();windows=os.name=='nt'
host='windows-x86_64' if windows else ('darwin-x86_64' if __import__('sys').platform=='darwin' else 'linux-x86_64')
tool=ndk/'toolchains/llvm/prebuilt'/host/'bin'
triple='aarch64-linux-android' if a.abi=='arm64-v8a' else 'x86_64-linux-android'
ninja=a.ninja or shutil.which('ninja') or 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
bash=a.bash or ('D:/Git/bin/bash.exe' if windows and Path('D:/Git/bin/bash.exe').exists() else shutil.which('bash'))
make=a.make or (ndk/'prebuilt/windows-x86_64/bin/make.exe' if windows else Path(shutil.which('make') or 'make'))
env={k.upper():v for k,v in os.environ.items()} if windows else dict(os.environ)
if windows:env['PATH']=(Path(bash).parent.parent/'usr/bin').as_posix()+';'+make.parent.as_posix()+';'+env.get('PATH','')
def run(command,cwd=None):
    print('RUN',Path(str(command[0])).name,command[1] if len(command)>1 else '',flush=True)
    subprocess.run([str(x) for x in command],cwd=cwd,env=env,check=True)
common=['-G','Ninja',f'-DCMAKE_MAKE_PROGRAM={ninja}',f'-DCMAKE_TOOLCHAIN_FILE={(ndk/"build/cmake/android.toolchain.cmake").as_posix()}',f'-DANDROID_ABI={a.abi}','-DANDROID_PLATFORM=android-28','-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF','-DCMAKE_POSITION_INDEPENDENT_CODE=ON','-DBUILD_TESTING=OFF','-DCMAKE_POLICY_VERSION_MINIMUM=3.5',f'-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}']
for name,flags in [('libogg-1.3.5',['-DINSTALL_DOCS=OFF']),('opus-1.5.2',['-DOPUS_BUILD_TESTING=OFF','-DOPUS_BUILD_PROGRAMS=OFF']),('libvorbis-1.3.7',[f'-DOGG_INCLUDE_DIR={prefix.as_posix()}/include',f'-DOGG_LIBRARY={prefix.as_posix()}/lib/libogg.a'])]:
    build=root/f'build-codec-{a.abi}'/name
    run([a.cmake,'-S',deps/name,'-B',build,*common,*flags]);run([a.cmake,'--build',build,'--parallel',a.jobs]);run([a.cmake,'--install',build])
for name in ('vorbis','vorbisenc'):
    file=prefix/'lib/pkgconfig'/f'{name}.pc';s=file.read_text(encoding='utf8')
    if 'Libs.private:' not in s:s+='\nLibs.private: -lm\n'
    file.write_text(s,encoding='utf8',newline='\n')
ext='.exe' if windows else ''
cc=f'{tool.as_posix()}/clang{ext} --target={triple}28';cxx=f'{tool.as_posix()}/clang++{ext} --target={triple}28'
env.update(CC=cc,CXX=cxx,AR=f'{tool.as_posix()}/llvm-ar{ext}',RANLIB=f'{tool.as_posix()}/llvm-ranlib{ext}',LD=cc,CFLAGS='-fPIC -O2',CXXFLAGS='-fPIC -O2',LDFLAGS='-Wl,-z,max-page-size=16384')
for name,flags in [('lame-3.100',[f'--host={triple}','--disable-shared','--enable-static','--disable-frontend','--disable-decoder','--with-pic','--enable-nasm=no']),('x264-stable',[f'--host={triple}','--enable-static','--enable-pic','--disable-cli','--disable-asm']),('libvpx-1.15.2',['--target=generic-gnu','--disable-examples','--disable-tools','--disable-docs','--disable-unit-tests','--enable-pic','--disable-runtime-cpu-detect','--enable-vp9-highbitdepth'])]:
    build=root/f'build-codec-{a.abi}'/name;build.mkdir(parents=True,exist_ok=True)
    if not (build/'Makefile').exists():run([bash,(deps/name/'configure').as_posix(),f'--prefix={prefix.as_posix()}',*flags],build)
    if windows:
        for file in build.rglob('*'):
            if file.is_file() and (file.name in ('Makefile','config.mak','config.mk','config.sh') or file.suffix=='.mk'):
                s=file.read_text(encoding='utf8',errors='replace');s=re.sub(r'/([a-z])/',lambda m:m[1].upper()+':/',s);file.write_text(s,encoding='utf8',newline='\n')
    extra=[f'SHELL={(Path(bash).parent.parent/"usr/bin/sh.exe").as_posix()}',f'MAKE={make.as_posix()}'] if windows else []
    for target in ('all','install'):run([make,f'-j{a.jobs}',*extra,target],build)
print('All Android codecs ready',a.abi,flush=True)
