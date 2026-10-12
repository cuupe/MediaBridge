# 第三方依赖

MediaBridge 应用源码采用 MIT 许可证（LICENSE）。发行包链接 GPL 版本的 FFmpeg，组合程序按 GPL v3 条款分发；应用源码的 MIT 许可继续保留。许可证文本随源码和 Windows 运行包提供。

## Windows

- FFmpeg 9.0.2，Gyan full shared build（GPL v3），来源：https://www.gyan.dev/ffmpeg/builds/ 。源码与构建信息：https://ffmpeg.org/download.html 、https://github.com/FFmpeg/FFmpeg/tree/n9.0.2 。
- Qt 6.9.2，Base / Declarative / Quick Controls / Quick Dialogs，共享库。源码：https://download.qt.io/archive/qt/6.9/6.9.2/single/ 。测试构建另链接 Qt Test。
- Microsoft Visual C++ Runtime，按 Microsoft 相应条款分发。

## Android

- Qt 6.9.3，Base / Declarative / Quick Controls / Quick Dialogs，共享库；LGPL v3 / GPL 可选条款及 Qt 工具例外。对应完整源码：https://download.qt.io/archive/qt/6.9/6.9.3/single/ 。
- FFmpeg 7.1.1，共享库，启用 GPL / version3，链接以下静态编码库。
- x264 stable 源码快照，GPL v2 或更新版本；快照归档及 SHA-256 随依赖源码包提供。
- libvpx 1.15.2，BSD 许可及专利授权，见 licenses/libvpx-*。
- LAME 3.100，LGPL，见 licenses/lame-*。
- Opus 1.5.2、libogg 1.3.5、libvorbis 1.3.7，BSD 类许可，见各 COPYING 文本。
- NDK libc++ 共享运行库，Apache 2.0 含 LLVM 例外；文本见 licenses/NDK-libcxx-NOTICE.txt。

Android 编码库对应源码归档、修改后的 FFmpeg 构建文件、各 ABI 的实际配置及构建脚本提供在 MediaBridge-2.0.3-android-codec-source.zip。MediaBridge 自身的全部对应源码另见 MediaBridge-2.0.3-source.zip。发布签名密钥不包含在源码包中。

Qt 和 FFmpeg 使用共享库链接；可从所列版本源码重建替代库并重新构建应用。Qt 许可文本见 LGPL-3.0-only.txt、GPL-3.0-only.txt、Qt-GPL-exception-1.0.txt。第三方 SDK 的完整内部依赖和许可由相应上游源码及供应方构建信息定义。
