# ABI 2 迁移

本轮是 2.0.0 破坏性升级。ABI 代际固定为 2，不支持加载旧库后继续使用新签名。
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