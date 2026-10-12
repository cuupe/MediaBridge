# MediaBridge 2.0 实现说明

核心接收 UTF-8 本地路径；CLI 和 Qt GUI 使用同一套 FFmpeg 处理代码。库直接链接，没有 shell 命令拼接。

## 队列

TaskModel 是 QAbstractListModel，每个 Task 保存 UUID、输入与输出 URL、独立方案、选择状态、详情、进度、结果路径和错误信息。所有模型变更位于 GUI 线程；每个活动 Job 拥有独立线程和原子取消标志。工作线程通过 queued invocation 回传状态，不访问可变的任务容器。

调度器按队列顺序启动任务，并行上限 1–4。单项失败不会阻塞后续任务。暂停只停止调度；全部取消同时关闭调度和取消活动任务。删除操作保护活动任务与未导出结果。析构等待线程结束。

QSaveFile 原子保存 queue.json。启动时将 running / inspecting 复原为 queued，不自动执行。settings.ini 使用 QSettings，保存输出规则、性能、主题、当前方案及自定义方案。完整应用内结果独立存放在 pending 目录，即使禁用队列恢复，也会恢复未导出结果。

方案修改不改变活动任务。applyProfile 只更新选中的可编辑任务；loadTaskProfile 支持逐项编辑。预设 JSON 仅接收已知参数，参数范围与编码器兼容性由核心验证。输入探测在线程中进行，并显示流索引供高级选择使用。

## 媒体处理

每个并行任务独立执行：输入探测 → 解码 → 高级滤镜 → 编码 → 临时复用 → drain / trailer → 本地提交或 SAF 导出。无损封装直接复制选定的压缩包，绕过编解码器。

输入公共原点来自 AVFormatContext.start_time。解码帧按原点归一化，trim / atrim 后调整时间及速度，保留音画相对偏移；atempo 保持音调。图像依次进行去隔行、裁剪、方向 / 翻转、尺寸适配、色彩、帧率与淡入淡出。音频依次变速、动态 loudnorm、增益、淡入淡出与样本格式 / 采样率 / 声道转换。

编码器支持的像素格式、采样格式和采样率通过 FFmpeg API 查询。CRF 与码率模式分别配置；时间戳在滤镜、编码器和复用器间换算。EOF 依次 drain 解码器、滤镜和编码器，保留尾部帧与样本。

## 输出与平台

本地输出在同目录专用临时目录生成，成功写 trailer 和 close 后提交。Windows 使用 MoveFileExW；POSIX 覆盖使用 rename，无覆盖使用 hard link。失败和取消由 RAII 清理本任务临时目录，原目标不会提前删除。GUI 在调度时解决输出命名冲突；核心再次保护提交时的竞争。

Android 输入 content URI 由 Qt QFile 流式缓存。目录输出通过 QJniObject 调用 ContentResolver / DocumentsContract.createDocument 创建子文档，再用 ParcelFileDescriptor.detachFd 交给 QFile 写入。Qt 原生 FolderDialog / FileDialog 授权目录与文件，尝试持久化 URI 访问权限；部分提供者仅支持临时授权，重启后需重新选择输入。

SAF 不能承诺原子替换，完整结果先保存在 pending，导出 flush 成功后才清理副本。所有未导出结果跨重启保留；断电或提供者失败可能留下部分系统文档，应用内完整结果仍可重试。系统输出目录只新建文档，重名由提供者决定。

界面用 Qt Quick 的响应式布局、全窗口 DropArea 和原生多文件选择器。桌面侧栏在窄屏隐藏，使用顶部标签导航。所有控件操作由同一个控制器执行。

## 验证入口

- core_checks：参数 / 输出保护和即时取消检查。
- integration.py：真实音视频编码、解码、变换、流处理和 CLI 批量回归。
- queue_checks / queue_runner.py：真实转换、并行、暂停、失败、恢复、设置、方案与结果保护。
- gui_smoke.py：Qt 窗口接收真实 QDragEnter / QDrop、鼠标 / 键盘事件，检查编辑、应用方案、主题和转换；生成桌面与窄屏截图。
- main 的 --screenshot / --page / --section / --compact 为渲染验证入口；--self-test / --ui-test 为自动化验证入口。
