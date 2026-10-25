# 截图方式与平台限制

主仓库：[AbyssRei/maa-capture](https://github.com/AbyssRei/maa-capture)。方式选择参考[上游控制方式说明](https://github.com/AbyssRei/MaaFramework/blob/main/docs/zh_cn/2.4-%E6%8E%A7%E5%88%B6%E6%96%B9%E5%BC%8F%E8%AF%B4%E6%98%8E.md#win32-screencap)，这里使用本库实际的 API 和位掩码。

## 位掩码与测速

| 方式 | 值 | 平台 | 截图范围与限制 |
|---|---:|---|---|
| GDI | 1 | Windows | 窗口 DC，依赖目标的绘制方式，通常需要窗口可见 |
| FramePool | 2 | Windows | WGC 窗口捕获，可用于后台；本库带伪最小化辅助 |
| DXGI_DesktopDup | 4 | Windows | 一个显示器的完整桌面输出 |
| DXGI_DesktopDup_Window | 8 | Windows | 桌面复制后按窗口区域裁剪，包含可见遮挡效果 |
| PrintWindow | 16 | Windows | 让窗口绘制到缓冲区，可用于后台；兼容性依赖目标 |
| ScreenDC | 32 | Windows | 从屏幕 DC 读取窗口所在区域，需要可见画面 |
| PipeWire | 64 | Linux | 已授权或已提供的视频源节点，其范围由生产者决定 |
| Wlr | 128 | Linux | wlroots 的一个 `wl_output`，需要 wlr-screencopy 协议 |
| ScreenCaptureKit | 256 | macOS | CGWindowID 对应的窗口或一个显示器 |

C++ 名称为 `mws::Method_*`，C 为 `MAA_WS_METHOD_*`，Python 为 `Manager.METHOD_*`。其他绑定的常量可能只覆盖 Windows 方式，见[绑定文档](../bindings/README.md)。

`Foreground` = DXGI 窗口裁剪 | ScreenDC，`Background` = FramePool | PrintWindow。`All` 包含全部方式位，构建及运行环境不支持的方式会被忽略或在测速中淘汰。`None` 仅使用外部注册单元。

`connect()` 对候选方式预热和测速。未选中的方式会停用以释放临时状态，选中者用于后续截图。测速不判断图像是否符合业务预期；黑色或冻结画面仍可能是一次成功的 API 调用。

## Windows

不存在适用于所有窗口的截图方式。FramePool / PrintWindow 通常更适合后台窗口；GDI / ScreenDC / DXGI 窗口裁剪通常依赖可见内容。受保护内容、远程桌面状态和驱动能力可能限制结果。

FramePool 的构建需要 Windows 10 SDK 10.0.22000+ 及 C++/WinRT；SDK 不满足或 `MWS_ENABLE_FRAMEPOOL=OFF` 时不会报告为可用方式。运行时仍需系统与目标支持 WGC。

FramePool 与 PrintWindow 的伪最小化会在需要时把最小化窗口恢复为透明、点击穿透的状态，以允许目标继续绘制。`inactive()` / 关闭管理器时停止辅助监控，恢复属性及最小化意图；再次截图时会重新启用辅助。目标停止渲染、独占全屏或自身限制后台绘制时，不能保证获取新帧。

DXGI 窗口裁剪会确保窗口位于显示器可截取范围，必要时可能调整位置和尺寸；与显示器边界重合的无边框全屏窗口会保留原有几何信息。整屏复制本身不等于独立窗口捕获。

通常不需要管理员权限。遇到失败先核对 HWND、宿主和目标的权限、桌面会话及目标的绘制方式；提高权限不能保证捕获受保护内容。

## Linux

### PipeWire：连接方式与截图范围

Portal FD 模式使用调用方已经取得的 `fd + node_id`。调用方负责 ScreenCast 会话、授权选择及会话存活；本库不会弹出授权界面。FD 在连接时交给 PipeWire 管理，之后调用方不能再次关闭或复用它。

会话 daemon 模式使用 `fd=-1` 和当前会话内已有、可访问的视频节点，例如 gamescope 输出。它不会根据 PID、X11 窗口号或标题自动创建视频源。

两种模式都接收该节点提供的完整流。窗口、显示器或其他合成输出的范围由生产者决定；如果 daemon 已提供单窗口源，它也可以用于窗口级截图。Portal 能提供哪些源由桌面后端决定，不能假设所有桌面都能把全部显示器拼成一个流。

有效宽高取自格式协商，库按该尺寸复制整帧并转换为 BGR。不会再按调用方的窗口矩形裁剪或缩放；流中已有的缩放、边框或黑边会保留。`set_monitor_index()` 不影响 PipeWire 来源。

### Wlr 与 X11

Wlr 使用 `WAYLAND_DISPLAY` 或 `set_wlr_socket()` 指定的 socket，截取一个输出。只有支持 wlr-screencopy 的合成器可以使用，不能仅根据“运行在 Wayland”就推断可用。

当前内置后端没有通用 X11 窗口句柄截图接口。需要其他来源时，可通过外部回调提供图像。

### 系统依赖

Debian / Ubuntu 源码构建示例：

```bash
sudo apt-get install libpipewire-0.3-dev libdrm-dev libwayland-dev pkg-config
```

官方 Python wheel 的 Linux 库启用了对应后端，运行时仍需 PipeWire、libdrm、Wayland 客户端运行库。源码构建时依赖缺失会禁用对应后端；可用方式为 0 时，仍可使用自定义回调。

## macOS

要求 macOS 14+ SDK 和可用的 ScreenCaptureKit。宿主应用需要获得屏幕录制权限；终端启动的 Python 程序需检查实际宿主的授权。纯截图不使用输入控制的辅助功能权限。

`hwnd` 参数在 macOS 承载 CGWindowID，`None` / 0 表示显示器截图，不是 HWND 或 Objective-C 对象指针。Python 的 `find_window_by_title` / `foreground_window` 只在 Windows 导出，不能拿来查 macOS 窗口。

## 显示器索引与查询

| 平台 | 索引语义 | 特别说明 |
|---|---|---|
| Windows | 0 为主显示器，其他按枚举顺序 | DXGI 整屏中显式索引优先于 HWND；越界回退主屏 |
| Linux Wlr | 合成器广播的输出顺序 | 不影响 PipeWire；需要可连接的 Wayland |
| macOS | ScreenCaptureKit 的显示器顺序 | 作用于显示器截图，不改变已选窗口 |

`monitor_count()` / `monitor_info()` 无需先 `connect()`。无可用显示器、没有权限或后端未编入时可返回 0 / None；`monitor_info()` 越界返回 None。Linux 的设备名称目前为空。

`available_methods()` 是构建能力位掩码，不能替代会话、权限、目标及真实截图验证。
