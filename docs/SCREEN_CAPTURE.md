# 来源、坐标与像素

| 平台/来源 | 桌面单位 | 输出 |
|---|---|---|
| Windows monitor | 物理像素，允许负原点，X 向右/Y 向下 | DXGI 原生网格 |
| Windows window | 物理像素客户区 | BGR 客户区；不合成其他 popup |
| macOS monitor | CoreGraphics 桌面 point，X 向右/Y 向下 | 过滤器 pointPixelScale 对应网格 |
| macOS window | point，过滤器内容边界 | 无阴影的独立窗口；保留窗口边框 |
| PipeWire/Wlr/custom | provider-defined | 来源实际提供的像素网格 |

content_rect 使用输出图像像素。desktop_rect 使用 coordinate_unit/space 声明的单位；
frame.scale_x/y 是从该桌面单位到输出像素的映射。
monitor snapshot.scale_x/y 表示系统当前配置的像素比例/缩放，可用性由 valid_fields 标记。
因此 Windows 的 150% 显示器快照可能 scale=1.5，但物理像素帧映射仍为 1。

Windows 通过 QueryDisplayConfig 的设备路径关联 HMONITOR 和实际 DXGI 输出，
D3D 设备创建在输出所属适配器。DISPLAYn 和展示名称不作身份。
快照 DPI 查询和捕获临时使用线程的 per-monitor-v2 上下文并恢复，不修改宿主进程 DPI。
镜像输出标记歧义；远程/虚拟输出保守标记会话级身份。
persistent 表示设备路径/UUID 的设备级范围，不保证驱动重装、重新接线后永远不变。

DXGI 未实现旋转缓冲纠正时返回 UNSUPPORTED_GEOMETRY。
DXGI 窗口裁剪要求客户区完全落在选定输出，不会移动/缩小窗口来凑尺寸。
ScreenDC 和 DXGI 裁剪表示可见桌面内容，可能受到其他窗口遮挡；
FramePool/PrintWindow 具备后台路径，但实际权限和应用支持仍需验证。
FramePool 在专属 MTA 线程使用无 DispatcherQueue 依赖的帧池；无新帧时可返回 TIMEOUT，
不会用旧帧冒充本次捕获。伪最小化的样式恢复在下一次截图或停用时由调用线程执行。

macOS 使用设备 UUID、原生 display ID 和当前过滤器关联；
主屏通过 CGMainDisplayID 判断。混合 DPR 跨屏窗口不能用单一映射表示时拒绝。
不构造跨屏统一物理像素平面，不放大 point 尺寸截图后标记为 native。
异步结果通过共享状态传递，超时后的回调不访问管理器或调用栈。

Linux 本轮没有稳定设备身份承诺。Wlr 使用同一 Wayland 连接选中的 output，
不提供热插拔重绑定；PipeWire 的来源与范围来自外部授权声明，
调用方负责 Portal 会话。FD 连接后按 PipeWire 所有权规则移交，不能重复交给新连接。
使用过的 Portal FD 在停用/连接失败后不能自动重连到会话 daemon；
此时返回 INVALID_STATE，须由调用方重新授权、取得新 FD 并创建新管理器。
通用 monitor 快照在 Linux 明确返回 UNSUPPORTED_OPTION。

几何定义依据 [Windows QueryDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig)、
[Apple SCContentFilter](https://developer.apple.com/documentation/screencapturekit/sccontentfilter)。
实际平台验收状态见 [验证记录](CAPTURE_SOURCE_VALIDATION.md)。
