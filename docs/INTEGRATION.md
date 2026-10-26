# ABI 2 集成契约

管理器创建时必须提交一个带标签的 CaptureTarget。monitor 使用设备 key，window 使用
平台和 HWND/CGWindowID，可附进程 ID；PipeWire 和 Wlr 使用外部授权/会话配置，
custom 使用 provider key 和范围声明。创建后目标与方法配置不可变，更换来源须重建。

methods=0 由目标范围和编译能力派生候选。显式选择先逐位验证；
未知、未编入或跨范围方法直接失败，不静默丢弃。
能力查询仅描述编译情况及范围，connect 和实际取帧决定运行时可用性。

C API 的结构均使用固定宽度字段，struct_size 必须等于本代 sizeof；
reserved 必须为零。固定容量字符串为 UTF-8，容量含结尾 NUL；
返回结构按值复制，管理器选中名称的 const char* 仅到 inactive/destroy 前有效。
任意外部地址的可读性由调用方保证；库验证 NULL、尺寸、声明长度、步长和溢出，
不声称能证明指针后真实分配的内存长度。

Snapshot 独立于管理器且不可变，读取条目不会再次查询操作系统。
其顺序为主屏优先，其余按设备 key 排序；index 只用于当前快照展示。
layout_token 覆盖身份、主屏、几何、缩放和旋转，不可作为持久硬件身份。

每次取帧核验当前目标、复制原生缓冲并再次核验相关身份与布局。
发现变化则丢弃像素。Windows 窗口核对 HWND、进程 ID/创建时间及客户区；
macOS 窗口核对 CGWindowID、拥有进程和过滤器几何。采样不能排除采样间的所有瞬态变化。

成功帧包含 BGR 像素、实际方法、来源 key、坐标约定、内容矩形、布局 token 和限制。
C FrameHandle 独立于管理器；必须单独 FrameDestroy。
Python 返回冻结值对象和 bytes，finally 释放原生帧。
C++ ScreencapBase::screencap 返回 optional<CaptureFrame>，last_error 提供失败详情。
自定义 C++ 单元只可注册到对应 custom 目标，并须返回同一来源声明。

同一管理器须由宿主串行使用，不提供全局锁。inactive 释放资源/恢复临时窗口状态，
重新使用须 connect；Destroy/RAII/close 负责最终释放。
最后一次失败保留到下一次失败；读取 last_error 不清除它。

Windows 的临时样式与最小化意图由同一进程、同一已加载库实例内的 HWND 租约协调。
不同 Manager 可以由各自宿主线程串行操作同一窗口；每个窗口独立保护状态转换。
首次实际修改时才记录 layered/alpha/色键/flags；最后一个修改持有者退出时恢复本库新增的位，
保留其他样式位的外部变化。目标在前台且观察到新的用户输入时取消最小化意图；
单独的程序化前台切换不能可靠区分于本库还原后的延迟系统消息，不据此猜测用户意图。
无法读取原 layered 属性时拒绝修改，外部属性所有权不明或恢复失败时保留结构化错误。
库使用私有窗口属性标记已观察的窗口生命周期，并核对 PID、线程及进程创建时间；
无法写入标记时拒绝声称身份已验证。窗口销毁后属性消失，旧租约禁止恢复到替换窗口。
这不协调其他进程或另一份独立加载的库，也不能排除两次采样间的全部外部竞争。

connect 成功后保留正式会话的独立首帧，首次 screencap 前复验身份、布局、几何和会话。
移动/缩放等变化会废弃首帧并重新采样；inactive/reconnect/destroy 清空暂存，已交付 FrameHandle 继续有效。
候选冷启动只有一个有效样本时按该次真实取样耗时排序；诊断记录样本数，不伪造稳定采样耗时。
固定方式不测速、不换方式。默认 auto 及前台候选仍可能激活目标。

### 显式 FramePool 缓存扩展（未发布）

旧 MaaWSCaptureOptions、MaaWSFrameInfo 的布局与精确 struct_size 检查不变，reserved 仍须为零。
先查询 MaaWSGetApiFeatures：MAA_WS_FEATURE_SAMPLE_INFO 表示有帧取样信息查询；
MAA_WS_FEATURE_FRAMEPOOL_CACHE 表示本构建支持显式 FramePool 缓存策略。
未编入 FramePool 或其他平台不报告缓存能力，拒绝策略，不能静默忽略。

```c
MaaWSCapturePolicy policy = {sizeof(MaaWSCapturePolicy), 0, MAA_WS_POLICY_FRAMEPOOL_CACHE};
/* Only after checking MAA_WS_FEATURE_FRAMEPOOL_CACHE. options remains ABI 2. */
MaaWSManagerHandle manager = MaaWSManagerCreateWithPolicy(&options, &policy, &error);
MaaWSFrameSampleInfo sample = {0}; sample.struct_size = sizeof(sample);
/* frame has independent ownership, including after manager destruction. */
MaaWSFrameGetSampleInfo(frame, &sample);
```

策略创建后不可变。仅允许复用该后端/设备/捕获会话中已通过来源、布局、客户区及裁剪验证的完整帧，
仅无更新可回退；无有效首帧仍超时。权限、设备、来源、尺寸及会话失效不能以缓存掩盖。
FramePool 仍清旧队列并有界等待。显式缓存会在到帧后于原工作线程重新采样并验证缓存；
请求与到帧交错时只接受同一已验证会话内的后续原生取样，防止延迟点击的更新被清队列丢失。
这不是旧入口的“直接返回队列最新帧”行为。未启用策略时没有后台缓存刷新。

frame_id 是 Manager 的成功交付序号。sample_id 是后端成功取样序号，可在交付之间推进，
二者都不能证明像素变化。sampled_at_ns 与既有 timestamp_ns 是库侧取样完成时间，
复用时保持；delivered_at_ns 单独记录本次交付。时间来自单调时钟，不能当作 UTC 或游戏渲染时刻。
MAA_WS_SAMPLE_NEW/REUSED 标记当前交付路径，CONNECT_PENDING 可与其组合；
后台已取得的新样本随后通过无更新回退交付时标记 REUSED，其 sample_id 可以高于上次交付。
session_id 是后端内部的会话代次，不用于跨 Manager 比较。
自定义 provider 及尚未实现原生取样信息的后端返回 UNKNOWN（取样序号/时间为零）；
CONNECT_PENDING 与交付时间仍可记录。元数据随独立帧保存。

Python 使用 Manager(..., framepool_cache=True)，返回 frame.sample；Node.js/C#/Java 使用 framepoolCache；
Go 使用 NewManagerWithPolicy，Rust 使用 Manager::new_with_policy。新绑定会先探测功能位，
旧 ABI 2 库继续走旧入口并返回未知采样信息，请求缓存则明确失败。
C++ CaptureFrame、基类及 Manager 布局已改变，必须和当前 SDK 成套重新编译；C ABI 仍为 2。
Rust 的完整结构字面量须提供新增的 sample（或使用 CaptureFrame::provider），
Go 应更新无键结构字面量或改用带字段名构造；这些公开 SDK/绑定变化按软件 MAJOR 发布。

### 可选增强策略（E1–E5，未发布）

高级入口 `MaaWSManagerCreateAdvanced` 使用独立的 `MaaWSAdvancedCapturePolicy`（48 字节）。
原 `MaaWSCapturePolicy` 仍为 16 字节，原创建入口仍只接受原来的 FramePool 开关。
先探测 `MAA_WS_FEATURE_ADVANCED_POLICY`，分别按 `DXGI_CACHE`、`STRICT_BACKGROUND`、
`FRAME_FRESHNESS`、`DEADLINES`、`CANCELLATION` 功能位判断能力。缺能力明确拒绝，不静默降级。

高级策略的 flags 为 `FRAMEPOOL_CACHE=1`、`DXGI_CACHE=2`、`STRICT_BACKGROUND=4`、
`FRESH_ONLY=8`、`DIAGNOSTICS=16`。新鲜度和时限字段单位为单调纳秒，零表示不设限。
所有配置创建后不可变。

- DXGI 缓存仅在 `DXGI_ERROR_WAIT_TIMEOUT` 的无更新路径复用；access lost、权限、设备、输出/布局/窗口裁剪变化均不能回退。全输出或窗口裁剪都保留原采样元数据及本会话来源。默认 DXGI 仍在无更新时超时。
- 严格后台在构造/试用之前过滤可能激活的 ScreenDC 与 DXGI 窗口方式，不通过“抢焦点再切回”实现。固定不合规方式明确失败；组合只在原授权集合中筛选。FramePool/PrintWindow 的最终最小化恢复使用不激活方式。同一输出的 DXGI 实例仍受系统 duplication 限制，不共享其他 Manager 的像素缓存。
- `MaaWSGetMethodBehavior` 独立报告 `NO_ACTIVATE`、`MAY_ACTIVATE`、`PSEUDO_MINIMIZE`、`COOPERATIVE_WAIT`、`UNCANCELLABLE_CALLS`。行为声明不代替编译可用性、来源范围或运行时连接验证；未知行为不是“不激活”承诺。当前严格后台只承诺本库不主动激活 Windows 目标，用户/目标自身的激活不在保证内。
- `fresh_only` 要求在当前调用开始后从后端重新取样，拒绝缓存重放及调用开始前的连接首帧。`max_sample_age_ns` 按原始 `sampled_at_ns` 计算，过龄返回 `MAA_WS_STALE_FRAME`。自定义来源没有可认证的原生采样时间，创建时明确拒绝这些策略。新取样仍不证明像素变化；内容变化信息保持未知。
- `connect_timeout_ns`、`capture_timeout_ns` 及 `first_frame_timeout_ns` 从一次调用开始形成绝对截止时间。连接首帧期限覆盖全部试用与正式首帧验证，和总连接期限取更早者，不能每个候选重置。超过期限返回 `MAA_WS_DEADLINE_EXCEEDED`。

`MaaWSManagerGetCancellationHandle` 返回单独拥有的取消句柄。`MaaWSCancelRequest` 可在其他线程请求取消当前调用，
空闲时返回 false，不污染下一次操作；返回 true 表示标记了活动调用，和已经完成的交付仍存在正常的完成竞争。
取消返回 `MAA_WS_CANCELLED`，退出时清空待交付首帧、释放捕获资源并恢复租约；恢复失败仍保留其诊断。
管理器销毁后句柄仍可查询（返回 false），必须单独 `MaaWSCancelHandleDestroy`。
获取句柄/管理器调用由宿主串行执行，不得在活动调用期间并发销毁 Manager；原始 C 句柄销毁也不得和同句柄 request 并发。
绑定提供 `Cancellation` 的独立生命周期，Rust 可克隆并发送该信号到其他线程，Go/Python/C#/Java 同步句柄关闭与请求。
Node.js 同步 FFI 会占用事件循环，取消可从原生回调或支持的工作线程集成中请求，不承诺普通 JS 定时器能打断同步 FFI。

| 调用部分 | 取消/期限行为 |
| --- | --- |
| DXGI `AcquireNextFrame` | 最长 20ms 短轮询，共用该次总截止时间；默认原生无更新等待仍为 2 秒 |
| FramePool 新帧等待 | 2ms 轮询检查，总截止时间覆盖队列清理及重试 |
| 候选切换、首帧发布、缓存/首帧交付 | 在边界检查同一取消状态及截止时间，晚到结果拒绝交付 |
| PrintWindow、GDI、Win32 身份/布局调用、设备初始化/映射、外部 provider 回调 | 不可保证在系统调用中安全中断；返回后检查并拒绝晚到结果，再进行清理 |

这些是协作时限，不能宣称 PrintWindow 等调用具有硬中断上限；不终止线程，也不把测试父进程超时当作库取消能力。
退出与恢复可能需要额外时间，优先完成状态恢复。

DIAGNOSTICS 增加结构化文本事件：候选冷启动耗时/单样本/失败 stage 与原生码、策略拒绝原因、
缓存命中/失效原因、采样/会话/交付序号与库侧延迟。重复缓存和交付事件仅记录前 4 次及之后的 2 次幂计数；
原有日志接收器仍可关闭/接管日志。不记录图像或窗口标题。公开诊断查询 API 继续单独设计。

```python
from maa_capture import CapturePolicy, Manager
manager = Manager(target=target, policy=CapturePolicy(
    framepool_cache=True, strict_background=True,
    max_sample_age_ns=5_000_000_000,
    connect_timeout_ns=5_000_000_000, capture_timeout_ns=500_000_000,
))
signal = manager.cancellation_handle()  # 在宿主线程获取
# 另一个线程可 signal.request()；完成竞争以调用的实际返回结果为准。
signal.close()
manager.close()
```

其他绑定的高级策略为 Node.js `policy` 对象、C# `CapturePolicy`、Java `CapturePolicy`、
Go `NewManagerAdvanced`、Rust `Manager::new_advanced`；六语言均保留旧默认构造路径。

参考 [迁移表](API_MIGRATION.md)、[平台几何](SCREEN_CAPTURE.md) 和
[公开 C 头](../include/maa-capture/MaaWSCustomScreencap.h)。
