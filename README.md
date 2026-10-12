# MediaBridge 2.0.3

基于 **C++17 / FFmpeg / Qt 6 Quick** 的跨平台音视频格式处理程序。Windows、Linux、Android 共享同一个转换核心；桌面端另有命令行入口。

## 2.0 工作台

- **批量队列**：多文件选择、桌面文件 / 文件夹拖入、文件夹导入、可选递归扫描、重复任务过滤、任务选择与排序。
- **任务管理**：1–4 个并行任务、独立进度、暂停 / 继续排队、单任务 / 全部取消、失败重试、清理已完成任务、JSON 报告。
- **恢复与方案**：队列和历史保存在本地，重启后恢复未完成任务；每项任务保存独立参数。内置标准、1080p、压缩、MP3、无损封装方案，自定义方案可保存、删除、导入和导出。
- **12 种格式**：MP4、MKV、WebM、MOV、AVI、TS、M4A、MP3、WAV、FLAC、OGG、Opus。转码、音频提取与无损封装共享 C++ 核心。
- **视频**：可用编码器选择、码率 / CRF、编码速度、GOP、线程数、宽高与帧率、留边 / 填满 / 拉伸、区域裁剪、90° 旋转、自动方向校正、镜像、去隔行、亮度 / 对比度 / 饱和度。
- **音频与时间**：编码器、码率、采样率、保留声道 / 单声道 / 双声道、音量增益、单遍动态响度标准化、精确截取、0.25–4 倍保持音调变速、音视频淡入淡出。
- **高级流处理**：视频 / 音频流索引、移除视频或音频、复制 / 清除元数据、标题 / 作者覆盖、无损封装时复制兼容字幕流。
- **设置**：输出目录、命名模板、同名重命名 / 跳过 / 成功后替换、默认线程、并行数量、递归导入、启动恢复、深色 / 浅色 / 系统主题。
- **平台文件处理**：中文路径、原生选择器、Android SAF 多文件输入与目录输出、所有未导出结果恢复。FFmpeg 库直接链接，转换不启动 shell 或 ffmpeg.exe。
- **输出保护**：本地输出完成 trailer 和关闭后才原子提交；失败 / 取消保留原目标文件。Android 系统导出失败时保留完整应用内结果。
- **手机屏幕适配**：按可用宽高调整横竖屏、长屏、小屏、平板、折叠和分屏窗口；导航、底栏及弹出菜单使用四边安全区域。窄屏底栏换行、矮屏工作区滚动，输入时收起导航与底栏并避让软键盘。

预设与编码器依赖实际 FFmpeg 构建。界面按目标容器列出已编入的兼容候选编码器；mediabridge-cli presets / encoders 返回可用能力。MP4 / MKV 默认使用内置 MPEG-4 + AAC；“手机 1080p”和“节省空间”方案在有 libx264 时使用 H.264。

## 使用步骤

1. 添加或拖入多个媒体文件，程序自动读取实际格式和流信息；桌面可导入文件夹，未知扩展名按内容识别。
2. 在队列上方选择输出格式，原编码无法直接封装时自动切换为转码（如 PCM WAV 转 MP3）；或在“高级处理”载入方案、修改参数。设置立即同步到选中待处理任务；“完成设置”返回队列。活动任务保持启动时的参数副本。
3. 在“设置”选择输出位置和同名规则，点击“开始处理”。
4. 查看每个任务的结果或失败原因，必要时重试、另存 Android 结果或导出报告。

“暂停排队”停止启动新任务，活动转换继续完成。关闭活动任务时可选择先停止再退出；恢复任务需要手动点击开始。Android 请保持应用在前台。

## 环境

- CMake 3.24+、C++17 编译器。
- FFmpeg **7.1+ 的开发 SDK**，含 avformat / avcodec / avfilter / avutil / swscale / swresample 的头文件及共享库。
- GUI 需要 Qt 6.9+：Core、Gui、Quick、QuickControls2、QuickDialogs2。
- 测试额外使用 Python 3、`ffmpeg` 和 `ffprobe` 命令行工具。
- Android 使用 Qt for Android、Android SDK / NDK / JDK；具体版本必须匹配所选 Qt Kit。

仅有 `ffmpeg.exe` 无法编译本项目。应用编译完成后使用 FFmpeg 库，不再启动那个可执行文件。

## Windows

安装 Qt 的 MSVC 或 MinGW Kit，使用与 Kit 匹配的编译器。FFmpeg 的共享构建 SDK 可从 [FFmpeg 下载页](https://ffmpeg.org/download.html) 所列 Windows 构建提供方获取。将 `FFMPEG_ROOT` 指向含 `include/`、`lib/`、`bin/` 的目录。

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64 `
  -DFFMPEG_ROOT="C:/deps/ffmpeg" -DCMAKE_PREFIX_PATH="C:/Qt/6.9.2/msvc2022_64"
cmake --build build-windows --config Release --parallel
ctest --test-dir build-windows -C Release --output-on-failure
cmake --install build-windows --config Release --prefix "$PWD/dist/windows"
```

VS 2026 使用生成器 `Visual Studio 18 2026`。或者运行：

```powershell
./scripts/build-windows.ps1 -FFmpegRoot C:/deps/ffmpeg -QtRoot C:/Qt/6.9.2/msvc2022_64
```

Qt CMake 部署脚本会打包 Qt 运行库 / QML 插件，本项目另复制 FFmpeg DLL。使用 MinGW 时还需随程序提供编译器运行库；MSVC 发行版需相应 VC++ 运行库。

不构建界面：

```powershell
cmake -S . -B build-cli -G "MinGW Makefiles" `
  -DMEDIABRIDGE_GUI=OFF -DFFMPEG_ROOT="C:/deps/ffmpeg"
cmake --build build-cli --parallel
```

## Linux

安装发行版的 Qt 6 和 FFmpeg 开发包，**确认 FFmpeg 版本至少 7.1**。例如在提供所需版本的 Debian / Ubuntu 上：

```bash
sudo apt install cmake ninja-build g++ pkg-config python3 ffmpeg \
  libavformat-dev libavcodec-dev libavfilter-dev libavutil-dev \
  libswscale-dev libswresample-dev qt6-base-dev qt6-declarative-dev
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
./build-linux/MediaBridge
```

若发行版的 FFmpeg 较旧，可从官方源码构建共享 SDK，并使用 `-DFFMPEG_ROOT=/path/to/prefix`。Qt 非系统安装时设置 `-DCMAKE_PREFIX_PATH=/path/to/Qt/kit`。共享库在非系统目录时，设置 `LD_LIBRARY_PATH` 或通过包管理器打包相应依赖。

CLI：设置 `-DMEDIABRIDGE_GUI=OFF`，随后运行 `./build-linux/mediabridge-cli`。

## Android

已提供 Android 9+ 的安装包 `dist/MediaBridge-2.0.3-android.apk`，包含 arm64-v8a 与 x86_64 两种架构。12 种格式均有对应编码器。默认输出在应用内，完成后点击“另存结果”；在设置中选择系统目录可直接批量保存。应用需保持前台运行。

本次实际构建：Qt Android / Host 6.9.3、NDK r27c、SDK API 35、JDK 21、Gradle 8.12、FFmpeg 7.1.1。Qt 和所有打包共享库使用 16 KB 页对齐。

Windows 主机构建完整编码库：将 FFmpeg 7.1.1、x264、libvpx 1.15.2、LAME 3.100、Opus 1.5.2、libogg 1.3.5、libvorbis 1.3.7 解压到 `deps` 的对应版本目录。准备 Git Bash、GNU Make 4+、pkg-config、CMake 与 Ninja，然后运行 `scripts/build-android-codecs.py --abi arm64-v8a` 和 `scripts/build-ffmpeg-android.py --abi arm64-v8a`；x86_64 同理。工具路径可通过各脚本参数覆盖。脚本及依赖源码归档同时交付。

应用构建可使用 `scripts/build-android.ps1`，传入 Qt Android / Host、SDK / NDK / JDK 路径；传入 QtAndroidX64Root 可生成双架构包。发布 APK 需使用自己的签名密钥。


使用 **Qt Creator 的 Android Kit** 是最直接的构建方式。本项目使用 `android/AndroidManifest.xml` 模板，由 Qt 生成 Gradle 包装工程，支持 Activity 旋转与窗口大小调整。

### 1. 为目标 ABI 编译 FFmpeg

在 Linux / macOS / WSL 上，将官方 FFmpeg 源码放入 `deps/ffmpeg-source`，准备与 Qt Kit 匹配的 NDK：

```bash
bash scripts/build-ffmpeg-android.sh deps/ffmpeg-source "$ANDROID_NDK_ROOT" arm64-v8a 28
```

生成 `deps/ffmpeg-android/arm64-v8a/{include,lib}`。脚本支持 arm64-v8a、armeabi-v7a、x86_64、x86。不同 ABI 必须单独构建，不能使用 Windows DLL 或 Linux .so。

该基础构建提供 MPEG-4、AAC、PCM、FLAC，MP3 / WebM 需要自行编译外部依赖或供应完整 SDK。NDK 的链接参数包含 16 KB 页对齐；整个 APK 的兼容性仍取决于 Qt、所有依赖库和 NDK 版本，需在设备上验证。

### 2. 构建应用

Qt Creator 打开 `CMakeLists.txt`，选择 Android arm64 Kit，添加：

```text
FFMPEG_ROOT=/absolute/path/MediaBridge/deps/ffmpeg-android/arm64-v8a
MEDIABRIDGE_GUI=ON
```

然后构建、部署。CMake 的 `QT_ANDROID_EXTRA_LIBS` 会将 SDK 的 .so 打入 APK。

Linux 主机也可使用 Qt 的包装命令（路径替换为实际 Kit）：

```bash
/path/to/Qt/android_arm64_v8a/bin/qt-cmake -S . -B build-android -G Ninja \
  -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
  -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
  -DQT_HOST_PATH=/path/to/Qt/gcc_64 \
  -DFFMPEG_ROOT="$PWD/deps/ffmpeg-android/arm64-v8a"
cmake --build build-android --target apk --parallel
```

Android 最低 API 28（Android 9）。无需申请“所有文件访问权限”：输入和输出由用户通过系统文件选择器授权。转换时保持应用在前台；当前版本没有 Android 前台服务，系统在后台终止应用时不会保证任务完成。

Android 输入通常会占用一份缓存；输出也先生成应用内副本，因此要留出输入 + 输出所需空间。转换失败 / 取消时缓存自动清理，保存失败的完整输出留在应用数据目录，重启后可在相应任务点击“另存结果”。SAF 保存由文档提供方控制，无法承诺原子覆盖；保存阶段完成前会忽略取消请求。系统保存选择器可能提前建立一个空文档。

## 命令行示例

```bash
# 查看媒体信息（JSON）
mediabridge-cli probe input.mov

# 视频转 MP4
mediabridge-cli convert -i input.mov -o output.mp4 --format mp4

# 从第 10 秒开始处理 30 秒，并缩放到宽 1280
mediabridge-cli convert -i input.mp4 -o clip.mp4 --start 10 --duration 30 --width 1280

# 提取音频
mediabridge-cli convert -i input.mp4 -o audio.flac --format flac

# 无损换封装（保留所有音视频流，目标容器必须支持原编码）
mediabridge-cli convert -i input.mp4 -o output.mkv --format mkv --mode remux

# 使用额外的 H.264 编码器
mediabridge-cli convert -i input.mov -o output.mp4 --video-encoder libx264

# 明确允许覆盖；只有成功后才替换原输出
mediabridge-cli convert -i input.mp4 -o output.mp4 --overwrite
```

Ctrl+C 取消。退出码：0 成功、1 失败、2 取消。进度 / 错误写 stderr，probe / presets / encoders / batch JSON 写 stdout。批量 CLI 顺序处理，单文件失败不会中断后续文件。

## 处理规则和边界

- 转码默认选首个普通视频流和首个音频流，可指定流索引。音频默认双声道，设为 0 保留原声道布局。
- 换封装默认复制音视频流，可选择复制兼容字幕；不处理附件、章节和字幕编辑。
- 裁剪按源文件秒数解码处理，画面裁剪先于旋转。旋转自动校正启用时方向应用到像素并移除旋转矩阵；关闭时保留源矩阵。
- 指定宽高后按所选规则适配；所有视频尺寸为偶数。CRF 只适用于列出的支持它的编码器；容器与编解码器不兼容会显示失败原因。
- 音视频共用时间原点，保持相对偏移。未知时长会显示不确定进度；淡出需要已知或指定的时长。
- loudnorm 为单遍动态标准化，未提供双遍分析、HDR 色调映射、硬件编码、拼接 / 合并或烧录字幕。
- 仅处理本地媒体文件；不提供网络流、播放列表和任意 FFmpeg 命令执行入口。
- 文件夹导入过滤媒体扩展名，不跟随目录符号链接；队列最多 10000 项。
- Linux / Android 本地无覆盖提交使用硬链接，需要相应文件系统支持。SAF 系统目录使用 DocumentsContract 创建新文档，同名行为由提供者控制；本地冲突规则不会用于覆盖系统文档。
- 并行任务各有 FFmpeg 上下文和取消标志。暂停是排队暂停，不冻结活动编解码器。
- 程序在退出时等待活动线程终止；不是 Android 后台服务。设置和队列位于 QStandardPaths 应用数据目录，可用 MEDIABRIDGE_DATA_DIR 覆盖用于测试。
- 运行日志保留本次会话最近 500 条；任务报告可包含完整路径和处理参数。

## 项目结构

```text
include/mediabridge/media.hpp   可复用的 C++ API
src/core/media.cpp             FFmpeg 解复用 / 解码 / 滤镜 / 编码 / 复用
src/cli/main.cpp               Windows / Linux 命令行
src/gui/                       Qt 后台控制器和入口
qml/                           响应式中文工作台与可复用控件
cmake/FindFFmpeg.cmake         SDK / pkg-config 查找
scripts/                       Windows 与 Android 构建辅助
tests/                         真实媒体集成验证与取消验证
```

## 依赖许可

项目源代码采用 MIT，见 LICENSE。FFmpeg / Qt / 外部编码器的许可证独立于本项目；发行包须保留对应许可及来源。当前 Windows 本地验证使用了 Gyan 的 GPL 共享 FFmpeg 构建，不能按纯 MIT 的二进制包分发。自编 FFmpeg 基础脚本不启用 GPL / nonfree 编码器；发行合规仍取决于全部依赖及链接方式。

## 本次验证

2.0 已在 Windows 构建并完成真实媒体、队列与界面验证；运行目录位于 dist/windows。Linux / Android 尚未编译或进行实机验证。详细记录和截图见 [docs/verification.md](docs/verification.md)。

## 官方参考

- [FFmpeg 编解码 send / receive API](https://www.ffmpeg.org/doxygen/trunk/avcodec_8h_source.html)
- [Qt Android 部署和外部库](https://doc.qt.io/qt-6/deployment-android.html)
- [Qt QFile 的 Android content URI 支持](https://doc.qt.io/qt-6.8/qfile.html)
- [Qt Quick FileDialog](https://doc.qt.io/qt-6/qml-qtquick-dialogs-filedialog.html)

### 批量与高级 CLI

```bash
# 一个或多个文件 / 文件夹，自动重命名，JSON 任务报告写 stdout
mediabridge-cli batch -i ./input -i ./another.mov -o ./output --recursive --width 1280 --conflict rename
# 目标目录需要事先存在
mediabridge-cli batch -i one.mp4 -i two.mp4 -o ./output --format flac --sample-rate 48000 --channels 1
# H.264 CRF、画面填满、帧率和音频响度（需要 libx264）
mediabridge-cli convert -i input.mov -o processed.mp4 --video-encoder libx264 --crf 24 --width 1280 --height 720 --fit fill --fps 30 --normalize-audio --loudness -16
# 区域裁剪、方向校正、变速和淡入淡出
mediabridge-cli convert -i input.mp4 -o clip.mp4 --start 5 --duration 20 --crop-x 100 --crop-y 40 --crop-width 800 --crop-height 600 --auto-rotate --speed 1.5 --fade-in 0.5 --fade-out 0.5
```

所有参数见 mediabridge-cli --help。高级处理参考 [FFmpeg 滤镜文档](https://www.ffmpeg.org/ffmpeg-filters.html)，设置使用 [QSettings](https://doc.qt.io/qt-6/qsettings.html)，Android 文件处理依据 [SAF](https://developer.android.com/training/data-storage/shared/documents-files) 与 [DocumentsContract](https://developer.android.com/reference/android/provider/DocumentsContract)。
