# API 迁移

## 3.0.0：SDK/绑定升级，C ABI 保持 2

3.0.0 按软件 MAJOR 发布；原 C ABI 2 的结构布局、旧入口与默认策略兼容。
CMake SDK 消费者改用 `find_package(maa-capture 3 CONFIG REQUIRED)`，C++ 全量重编译，
不要混用 2.x 头文件/对象与 3.x C++ 动态库。SONAME/install-name 仍为 2。

| 消费方 | 从 2.x 迁移 |
| --- | --- |
| C | 原入口继续可用；新功能先查 `MaaWSGetApiFeatures`，新增策略/采样结构零初始化并设置 `struct_size` |
| C++ | 基类/管理器/帧布局与可复制性改变，重编译 SDK 与扩展 provider；新增策略默认为关闭 |
| Rust | `CaptureFrame` 新增 `sample`；完整字面量需补字段，自定义 provider 优先使用 `CaptureFrame::provider` |
| Go | `CaptureFrame` 新增 `Sample`；更新无键字面量或改为具名字段，自定义 provider 可使用 `ProviderFrame` |
| C# | `Manager` 构造器新增可选参数，源代码默认调用继续可用，预编译消费者须重新编译 |
| Python/Node.js/Java | 默认构造路径继续可用；按需选择新增采样元数据、策略与取消句柄 |

六语言绑定可继续使用原始 ABI 2 库的默认路径；请求库不具备的新策略、行为查询或取消功能时明确拒绝。
采样信息在旧库/自定义 provider 上可能未知，不得用交付序号或新样本标记推断内容变化。
独立取消信号可跨线程请求；Manager 及 provider 操作仍由单一宿主串行调用。
策略、期限与非协作系统调用限制见 [集成契约](INTEGRATION.md)。

与上游不同：2.0.0 起已移除伪最小化的独立约 100ms 轮询；当前在取图、逻辑最小化查询或释放租约时协调恢复。
当前判据还使用本库恢复后的前台快照与输入时间，不等同上游仅检查目标是否为前台。
暂停库操作期间，外部激活后的样式恢复可能延后；本轮没有将判据进一步收紧为必须有新输入。

## 2.0.0：ABI 1 → 2

2.0.0 是 ABI 1 → 2 的破坏性升级。ABI 代际固定为 2，不支持加载旧库后继续使用新签名。
Windows DLL、SDK、语言绑定必须成套升级，C/C++ 消费者必须重新编译。
CMake 使用 `find_package(maa-capture 2 CONFIG REQUIRED)`；
支持 SONAME/install-name 的平台使用库代际 2。

| ABI 1 入口/行为 | ABI 2 替代 |
|---|---|
| Manager(hwnd, methods) / 空 hwnd 推断整屏 | Manager(target=MonitorTarget/WindowTarget/...) |
| monitor_count/info 逐项系统枚举 | 独立不可变 MonitorSnapshot / monitors() |
| set_monitor_index | 从快照读取 monitor_key，创建 monitor 目标 |
| set_pipewire_target / set_wlr_socket | PipeWireTarget / WlrTarget，创建时一次配置 |
| set_builtin_methods / 默认 ALL | CaptureOptions.methods，0 表示范围内自动选择 |
| ImageBuffer.Info / 暂存像素指针 | CaptureFrame；C 用 FrameGetImage/GetInfo/Destroy |
| 旧自定义 ImageBuffer 回调 | FrameInput + 来源/几何/长度 + release_frame |
| 独立 CustomScreencap C 句柄 | CustomTarget 管理器并注册 provider |
| set_dll_path / dll_path | set_lib_path / lib_path |
| point 尺寸窗口默认输出 | 原生像素；无法表达的几何返回 UNSUPPORTED_GEOMETRY |
| 索引错误时主屏回退 | TARGET_NOT_FOUND / TARGET_AMBIGUOUS / SOURCE_MISMATCH |

C 新结构必须零初始化并设置 struct_size，reserved 必须为零。
字符串固定容量 UTF-8、含结尾 NUL；不能截断设备 key。
MaaWSManagerCreate 接收 CaptureOptions 指针及错误输出；
MaaWSManagerScreencap 输出独立帧句柄，失败会将输出置空。
每个成功帧都必须单独 Destroy，下一帧或 ManagerDestroy 不使旧帧失效。

回调表在注册时复制，注册后的函数地址和 context 由宿主持有至管理器销毁。
每次调用 screencap 后（包括失败）都会调用可选 release_frame；
数据必须保持可读直到该回调。管理器在此之前复制数据。
connect 可选；inactive 用于恢复临时状态。错误/来源不匹配的像素不会发布。

本库不管理 Qt 映射、窗口随程序选屏、应用配置或 Portal 授权。
布局核验是前后采样一致性检查，不是操作系统原子桌面事务。