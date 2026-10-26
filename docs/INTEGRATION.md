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

参考 [迁移表](API_MIGRATION.md)、[平台几何](SCREEN_CAPTURE.md) 和
[公开 C 头](../include/maa-capture/MaaWSCustomScreencap.h)。