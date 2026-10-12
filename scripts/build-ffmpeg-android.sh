#!/usr/bin/env bash
# Run on Linux/macOS/WSL, or Windows with Git Bash and GNU Make.
set -euo pipefail
if [[ $# -lt 3 || $# -gt 4 ]]; then
  echo "Usage: $0 FFMPEG_SOURCE ANDROID_NDK ABI [API=28]" >&2
  exit 1
fi
SOURCE=$(cd "$1" && pwd)
NDK=$(cd "$2" && pwd)
ABI=$3
API=${4:-28}
PROJECT=$(cd "$(dirname "$0")/.." && pwd)
case "$ABI" in
  arm64-v8a) ARCH=aarch64; TRIPLE=aarch64-linux-android ;;
  armeabi-v7a) ARCH=arm; TRIPLE=armv7a-linux-androideabi ;;
  x86_64) ARCH=x86_64; TRIPLE=x86_64-linux-android ;;
  x86) ARCH=x86; TRIPLE=i686-linux-android ;;
  *) echo "Unsupported ABI: $ABI" >&2; exit 1 ;;
esac
[[ "$API" =~ ^[0-9]+$ ]] && (( API >= 28 )) || { echo "API must be >= 28" >&2; exit 1; }
case "$(uname -s)" in
  Linux) HOST=linux-x86_64 ;;
  Darwin) HOST=darwin-x86_64 ;;
  MINGW*|MSYS*) HOST=windows-x86_64 ;;
  *) echo "Unsupported build host" >&2; exit 1 ;;
esac
if [[ "$HOST" == windows-x86_64 ]]; then
  [[ "$API" == 28 ]] || { echo "Windows full SDK script uses API 28" >&2; exit 1; }
  exec python "$PROJECT/scripts/build-ffmpeg-android.py" --source "$(cygpath -m "$SOURCE")" --ndk "$(cygpath -m "$NDK")" --abi "$ABI"
fi
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$HOST"
CC="$TOOLCHAIN/bin/${TRIPLE}${API}-clang"
CXX="$TOOLCHAIN/bin/${TRIPLE}${API}-clang++"
if [[ "$HOST" == windows-x86_64 ]]; then
  CC="$TOOLCHAIN/bin/clang.exe --target=${TRIPLE}${API}"
  CXX="$TOOLCHAIN/bin/clang++.exe --target=${TRIPLE}${API}"
  export PATH="$NDK/prebuilt/windows-x86_64/bin:$PATH"
fi
PREFIX="$PROJECT/deps/ffmpeg-android/$ABI"
BUILD="$PROJECT/build-ffmpeg-$ABI"
mkdir -p "$BUILD" "$PREFIX"
cd "$BUILD"
# Baseline uses built-in MPEG-4/AAC/PCM/FLAC, so no external encoder dependencies.
# MP3/WebM/H.264 become available if you supply a richer SDK built with those codecs.
"$SOURCE/configure" \
  --prefix="$PREFIX" --target-os=android --arch="$ARCH" \
  --enable-cross-compile --enable-shared --disable-static --enable-pic \
  --cc="$CC" \
  --cxx="$CXX" \
  --ar="$TOOLCHAIN/bin/llvm-ar" --ranlib="$TOOLCHAIN/bin/llvm-ranlib" \
  --strip="$TOOLCHAIN/bin/llvm-strip" --nm="$TOOLCHAIN/bin/llvm-nm" \
  --sysroot="$TOOLCHAIN/sysroot" --disable-autodetect \
  --disable-programs --disable-doc --disable-debug --disable-network --disable-x86asm \
  --disable-avdevice --disable-symver \
  --extra-cflags="-fPIC" \
  --extra-ldflags="-Wl,-z,max-page-size=16384"
JOBS=${JOBS:-8}
make -j"$JOBS"
make install
echo "FFmpeg SDK installed at $PREFIX"
