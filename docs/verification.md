# MediaBridge 2.0.3 验证记录

验证日期：2026-10-07（Asia/Shanghai）。

## 实际构建与回归

| 项目 | 结果 |
| --- | --- |
| Windows x64 MSVC 19.51 / Qt 6.9.2 / FFmpeg 9.0.2 | 编译通过 |
| 真实媒体与 CLI | 60 项通过，输出由独立 FFmpeg 7.1.1 解码 |
| 队列、参数同步、设置与恢复 | 67 项通过 |
| 桌面、手机及屏幕适配 GUI | 335 项通过，包含真实输入、转换及 17 组屏幕 / 安全区域切换 |
| Windows 独立运行包 | 限制 PATH 为 System32，清除 Qt SDK 变量后实际转换、GUI 交互与 CLI 批量通过 |
| Android arm64-v8a + x86_64 | Qt / Host 6.9.3、NDK r27c、SDK 35、JDK 21、Gradle 8.12 构建通过 |
| Android 转换核心既有覆盖（2.0.1） | x86_64 模拟器 12 种格式、未知后缀识别、截取 / 缩放 / CRF、无损换封装，共 15 项通过 |
| Android 2.0.2 原生 WAV 流程 | 日文文件名、191 秒 PCM WAV：文件可选、自动纠正无损封装、实际转换、系统另存和独立解码通过 |
| Android 2.0.2 覆盖升级 | 2.0.1 的真实 PCM 转 MP3 失败任务恢复为等待转码，重新开始、系统另存和独立解码通过 |
| Android 2.0.3 屏幕布局 | 正式签名 APK 在 9 组目标显示配置下检查四页导航和队列按钮；36 项操作检查和 4 项挖孔竖屏复测通过 |
| Android 2.0.3 输入 | 实际触摸滚动、数值字段聚焦、输入 160、完成设置、导航 / 底栏恢复通过 |
| Android APK | ZIP CRC、193 个共享库的 ELF 16KB 对齐、ZIP 16KB 对齐与发布签名均通过；正式签名 APK 实际安装、识别、转换、系统另存和独立解码通过 |
| Android 文件操作既有覆盖（2.0.1） | 多选真实媒体、实际格式识别、目录批量保存、同名重命名 / 跳过 / 覆盖、另存导出及未保存结果重启恢复通过 |

Windows 媒体检查覆盖延迟帧、音画偏移、中文路径、复制包哈希、输入输出保护、裁剪 / 旋转 / 适配 / 帧率 / 变速、采样率 / 声道 / 实测 PCM 音量 / 响度、淡入淡出、CRF、字幕、流索引和批量冲突行为。

新增队列检查覆盖错误后缀和 .data 文件的自动探测、立即同步参数、无损封装切回转码、格式切换清除不兼容编码器、WebM 的 CRF 编码器选择、音频格式清除视频 CRF、活动任务参数保持不变。

GUI 实际接收 Qt 拖放、鼠标和键盘事件；验证格式与模式快速选择、参数提交、主题、真实转换和输出尺寸。人工核对桌面、窄屏和深浅主题截图。手机内容区位于导航与页脚之间，Android 顶部状态栏安全区包含在导航高度中。

Android 实测设备为专用 x86_64 16KB 页模拟器，使用主机 GPU 的 OpenGL ES。原先 SwiftShader 虚拟驱动出现图形绘制异常，改用主机 GPU 后同一个 APK 正常绘制。媒体结果拉回 Windows 后由独立 FFmpeg 解码。2.0.1 的覆盖测试先把已有输出改为 15 字节测试内容，重启应用后从恢复队列转换，输出恢复为可解码视频，验证了输入和目录授权持久化。SAF 查名与写入阶段串行提交，避免并行任务同时写一个文档。2.0.1 构建同时转换两个同名目标，得到不同文档 URI 和 (1) / (2) 输出；五个目录输出均由独立 FFmpeg 解码通过。

## 2.0.3 屏幕适配

Qt Quick 回归在同一个窗口内实时切换 17 组配置：320×480、360×640、390×844、360×980，顶部刘海 390×844、挖孔 412×915、四边圆角安全区 360×780、折叠外屏 320×760，左右侧刘海 844×390、640×360、分屏 320×360、方形 480×480、展开屏 720×720、平板 800×1280 / 1280×800、展开窗口 1024×768。

272 项专项检查使用真实 `SafeArea.additionalMargins`，验证四边安全区域、开始按钮最小宽度、四个页面的真实鼠标导航、内容区不与导航和底栏重叠、表单保留滚动空间，以及键盘打开 / 关闭后的布局恢复。加上原有鼠标、键盘、真实转换及截图回归，共 335 项 GUI 检查通过。

内容区保留 [Qt 6.9 ApplicationWindow](https://doc.qt.io/archives/qt-6.9/qml-qtquick-controls-applicationwindow.html) 的自动安全区内边距；导航、底栏和弹出菜单额外按 [SafeArea](https://doc.qt.io/archives/qt-6.9/qml-qtquick-safearea.html) 的四边边距布局。表单输入框按文本高度和平台触摸尺寸计算高度，回归明确检查鼠标获得焦点和数值提交，防止适配过程中字段不可点击。

Android 正式 APK 覆盖安装成功，versionCode 5 / versionName 2.0.3。专用 x86_64 模拟器的目标物理显示配置为 1080×1920 / 420 dpi、720×960 / 360 dpi、800×1900 / 400 dpi、1600×1600 / 360 dpi、1600×2560 / 320 dpi，以及角部刘海竖屏、角部刘海左右横屏 1920×1080、挖孔竖屏 1080×2160。截图实际像素尺寸已核对；四个页面可切换、开始按钮可见。普通旋转、同一硬件刘海配置内的左右旋转及窗口 / 密度切换实时执行。

输入专项通过真实触摸滚动到“宽度 / px”，聚焦后 Android 输入法报告 `mInputShown=true`，导航和队列底栏收起；字段保持在可视表单中，输入 160 后点击完成设置，队列页面与底栏恢复。模拟器连接硬件键盘，输入法面板在截图中未正常绘制，因此截图仅证明应用字段与布局，未取得完整软键盘像素呈现的验证证据。

运行中切换 Android 开发者选项的“模拟显示刘海”系统资源覆盖，会强制重建 QtActivity，本测试环境出现空白界面。形状测试在预设刘海配置后冷启动应用，随后验证实时旋转；普通窗口变化不触发该问题。此项系统资源覆盖重建仍是已知测试限制。

## 既有 WAV 转 MP3 修复

以下 WAV 专项记录为 2.0.2 的 Android 原生验证；2.0.3 的 Windows / GUI 回归再次完成相同媒体转换。

截图中的 PCM WAV 被按无损封装处理，目标 MP3 容器不接受 PCM 编码。界面现在按实际选中音频、视频与字幕流判断容器兼容性，切换格式、选择处理方式、导入分析完成和开始处理时都会校正不兼容模式。CLI 明确指定 remux 时仍严格执行并拒绝不兼容输入，兼容 MP3 无损封装的压缩包哈希保持一致。

实际生成 191 秒 PCM WAV，并以日文文件名验证桌面和手机布局的拖入、MP3 选择、自动切回转码、下拉框同步与完整转换。输出由独立 FFmpeg 解码，FFprobe 确认编码为 MP3、时长完整。队列回归另外验证批量立即启动、旧失败任务直接恢复、旧版仅记录主流编码的元数据及修复模式的再次重启持久化。Windows 运行包在 PATH 仅含 System32、无 Qt SDK 环境变量的条件下重复上述真实转换。

Android 实际流程先在 2.0.1 导入未知后缀的 191 秒 PCM WAV，按 MP3 无损封装启动并复现同一英文错误。用正式签名 2.0.2 覆盖安装后，该任务自动恢复为等待转码，点击开始、系统另存均成功。随后在空队列保存 MP3 无损封装模式，通过系统选择器导入原始日文 WAV，确认文件为可选状态、识别完成即自动切回转码、最终显示已完成。两次系统导出文件各为 4,585,473 字节，由独立 FFmpeg 7.1.1 完整解码，FFprobe 确认 MP3 编码及约 191 秒时长。

Android 原生文件选择器使用全文件通配符，避免扩展名 MIME 类型映射限制把 WAV 置灰。此限制来自 Qt Android 对所有扩展名过滤器共同生成 MIME 列表的行为，参见 [Qt 6.9.3 源码](https://raw.githubusercontent.com/qt/qtbase/v6.9.3/src/plugins/platforms/android/qandroidplatformfiledialoghelper.cpp)。

## 复现

- ctest --test-dir build-msvc -C Release --output-on-failure
- python tests/package_smoke.py --package dist/windows --ffmpeg PATH --ffprobe PATH
- python tests/android_apk_checks.py dist/MediaBridge-2.0.3-android.apk
- python tests/android_media_smoke.py --help

GUI 入口（启用 MEDIABRIDGE_TESTS）：MediaBridge --ui-test ABSOLUTE_MEDIA；WAV 转 MP3 专项使用 --ui-test-audio ABSOLUTE_WAV，均可加入 --compact。

屏幕适配入口：MediaBridge --ui-test-layout ABSOLUTE_MEDIA ABSOLUTE_SCREENSHOT_DIR。`tests/gui_smoke.py` 自动执行此入口。Android 原生屏幕检查：`python tests/android_layout_smoke.py --adb PATH --device emulator-5556 --artifacts ABSOLUTE_DIRECTORY`，仅在专用测试模拟器使用，脚本完成后恢复默认显示配置。

截图入口：MediaBridge --compact --screenshot ABSOLUTE_PNG。离线 GUI 检查使用 QT_QPA_PLATFORM=offscreen、QT_QUICK_BACKEND=software；MEDIABRIDGE_DATA_DIR 将测试配置隔离。

## 交付与限制

- Windows：MediaBridge-2.0.3-windows-x64.zip，解压后运行 bin/MediaBridge.exe。
- Android：MediaBridge-2.0.3-android.apk，Android 9+，包含 arm64-v8a 和 x86_64。
- 应用全部对应源码：MediaBridge-2.0.3-source.zip。
- Android 编码库对应源码与实际构建配置：MediaBridge-2.0.3-android-codec-source.zip。

Android 已在模拟器实际安装运行；没有物理 ARM64 手机测试证据。Android 当前没有前台服务，长任务应保持应用在前台。SAF 文档提供方不能保证原子覆盖，保存失败会保留应用内完整结果。Linux 提供源码与构建入口，本环境未验证 Linux 二进制。

折叠屏测试针对外屏和展开后的窗口尺寸变化，未取得实体折叠设备或跨铰链显示区域的验证证据。

## 截图

- [桌面队列](screenshots-v2/desktop-queue.png)
- [手机空队列布局](screenshots-v2/mobile-empty.png)
- [设置](screenshots-v2/desktop-settings.png)
- [高级视频](screenshots-v2/desktop-video.png)
- [高级音频](screenshots-v2/desktop-audio.png)
- [Android 模拟器队列](screenshots-v2/android-queue.png)

- [正式 Android APK 转换完成](screenshots-v2/android-release.png)

- [Android 2.0.2 日文 WAV 转 MP3 已完成](screenshots-v2/android-audio-2.0.2.png)
- [Android 2.0.3 小屏](screenshots-v2/android-small-2.0.3.png)
- [Android 2.0.3 左侧刘海横屏](screenshots-v2/android-landscape-left-2.0.3.png)
- [Android 2.0.3 右侧刘海横屏](screenshots-v2/android-landscape-right-2.0.3.png)
- [Android 2.0.3 挖孔竖屏](screenshots-v2/android-hole-2.0.3.png)
- [Android 2.0.3 展开屏](screenshots-v2/android-fold-inner-2.0.3.png)
- [Android 2.0.3 平板](screenshots-v2/android-tablet-2.0.3.png)
- [Android 2.0.3 数值输入](screenshots-v2/android-input-2.0.3.png)
