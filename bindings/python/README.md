# maa-capture

[maa-capture](https://github.com/AbyssRei/maa-capture) 的 Python 绑定，使用标准库 ctypes，提供原生截图方式、测速管理器和自定义截图回调。

## 安装与平台

```bash
pip install --index-url https://pypi.org/simple maa-capture
pip install "maa-capture[numpy]"  # 可选
```

需要 Python 3.12+。官方 wheel 内置 Windows x64、Linux x64、macOS ARM64 原生库；其他架构需要自行构建并通过 `set_lib_path()` 配置。Linux 还需要 PipeWire、libdrm、Wayland 运行库；macOS 需要 macOS 14+ 和宿主的屏幕录制权限。

从源码 / editable 安装不会自动编译 C++ 库，需先构建本项目。详细要求见[构建文档](https://github.com/AbyssRei/maa-capture/blob/main/docs/BUILDING.md)。

## Windows 整屏或窗口

```python
from maa_capture import Manager, available_methods

print(hex(available_methods()))
with Manager(methods=Manager.METHOD_DXGI_DESKTOP_DUP) as capture:
    capture.set_monitor_index(0)  # 主显示器
    if not capture.connect():
        raise RuntimeError("无法连接截图来源")
    image = capture.screencap()
    if image is not None:
        print(capture.selected_unit_name(), image.width, image.height)
        pixels = image.data  # BGR bytes，已复制，可独立保留
```

指定窗口时可使用 Windows 专用的标题查找辅助：

```python
from maa_capture import Manager, find_window_by_title

hwnd = find_window_by_title("记事本")
if not hwnd:
    raise RuntimeError("未找到窗口")
with Manager(hwnd=hwnd, methods=Manager.METHOD_BACKGROUND) as capture:
    if capture.connect():
        image = capture.screencap()
```

`METHOD_BACKGROUND` 为 FramePool | PrintWindow；`METHOD_FOREGROUND` 为 DXGI 窗口裁剪 | ScreenDC。可以自行按位或组合。伪最小化会临时修改窗口显示属性，关闭或停用时恢复。

## macOS

```python
from maa_capture import Manager

with Manager(hwnd=None, methods=Manager.METHOD_SCREENCAPTUREKIT) as capture:
    capture.set_monitor_index(0)
    if capture.connect():
        image = capture.screencap()
```

`hwnd` 可传 CGWindowID 的整数值进行窗口截图，None / 0 表示显示器。Windows 的窗口查找辅助不会在 macOS 导出；本包没有 macOS 窗口枚举或权限申请界面。

## Linux

PipeWire 的授权和来源选择由调用方完成。以下 `portal_fd` 和 `node_id` 是调用方从 ScreenCast 会话取得的值：

```python
from maa_capture import Manager

with Manager(methods=Manager.METHOD_PIPEWIRE) as capture:
    capture.set_pipewire_target(fd=portal_fd, node_id=node_id)
    if capture.connect():
        image = capture.screencap()
```

连接当前会话已存在的视频源时使用 `fd=-1` 和有效节点 ID，例如 gamescope 的节点。FD 在连接时交给 PipeWire 管理，不能重复关闭或复用；调用方还需保持授权会话存活。

Wlr 用于支持 wlr-screencopy 的 Wayland 合成器：

```python
with Manager(methods=Manager.METHOD_WLR) as capture:
    capture.set_wlr_socket(None)  # WAYLAND_DISPLAY 默认 socket
    capture.set_monitor_index(0)
    if capture.connect():
        image = capture.screencap()
```

PipeWire 返回协商尺寸的整帧，窗口范围由上游来源决定；本库不二次裁剪，显示器索引也不改变 PipeWire 来源。Linux 的 `hwnd` 被忽略，内置后端没有通用 X11 窗口句柄截图接口。

## 显示器与方式查询

```python
from maa_capture import Manager, available_methods

print(hex(available_methods()))
with Manager(methods=Manager.METHOD_NONE) as capture:
    for index in range(capture.monitor_count()):
        print(index, capture.monitor_info(index))
```

`available_methods()` 表示构建能力，实际权限和来源可用性由 `connect()` 验证。显示器查询无需先连接；无权限、无 Wayland 或相关后端未编入时，数量可为 0。`monitor_info()` 越界返回 None，Linux 名称目前为空。

## 自定义回调与像素生命周期

```python
import ctypes
from maa_capture import ImageBuffer, Manager, ScreencapCallbacks

pixels = (ctypes.c_ubyte * 8)(10, 20, 30, 255, 40, 50, 60, 255)
def on_frame(_arg, out: ImageBuffer):
    out.data = ctypes.cast(pixels, ctypes.POINTER(ctypes.c_ubyte))
    out.width, out.height, out.channels, out.step = 2, 1, 4, 8
    return 1

callbacks = ScreencapCallbacks(screencap=on_frame)
with Manager(methods=Manager.METHOD_NONE) as capture:
    assert capture.register_custom_unit("gradient", callbacks)
    assert capture.connect()
    image = capture.screencap()
    assert image.data == bytes([10, 20, 30, 40, 50, 60])
```

像素缓冲应在原生调用复制完成前保持存活，不能只在回调内创建一个返回后立即失效的临时缓冲。管理器会保留已注册的回调和原生结构体引用。测速会调用回调多次，未选中的候选也会收到停用回调；停用实现应可重复调用。截图回调抛出的异常会记录到 Python 日志并转换为截图失败；其他回调也应自行处理异常。

## 关闭、线程与错误

优先用 `with Manager(...)`。也可显式 `close()`，它会停用并销毁原生管理器，可重复调用；关闭后的操作会抛出 RuntimeError。`inactive()` 只停用来源或恢复窗口状态，仍可继续使用管理器。

同一实例的配置、连接、截图和关闭需要串行化，GIL 不能替代原生对象同步。`connect()` 返回 False 表示没有候选成功；`screencap()` 返回 None 表示本次失败。成功调用也不保证业务画面没有黑屏或冻结，需由业务层判断。

`ImageBuffer.Info.data` 是 BGR 字节。安装 NumPy 扩展后，原始 `ImageBuffer` 的 `to_numpy()` 可转换图像；若使用 Manager 返回的 Info，可使用 `numpy.frombuffer(image.data, dtype=numpy.uint8).reshape(image.height, image.width, 3)`。

## 原生库加载顺序

1. `set_lib_path(path)` 指定的文件或目录。
2. `MAA_WS_LIB`，或兼容的 `MAA_WS_DLL_DIR`。
3. wheel 的 `_native/<platform>/` 内置库。
4. 系统动态库搜索路径。

创建管理器或查询构建方式时会触发加载。失败异常包含尝试过的路径，系统依赖缺失也可能导致已有文件无法加载。

## 文档与许可

[主仓库](https://github.com/AbyssRei/maa-capture) · [截图方式](https://github.com/AbyssRei/maa-capture/blob/main/docs/SCREEN_CAPTURE.md) · [集成与所有权](https://github.com/AbyssRei/maa-capture/blob/main/docs/INTEGRATION.md) · [故障排查](https://github.com/AbyssRei/maa-capture/blob/main/docs/TROUBLESHOOTING.md)

LGPL-3.0-or-later，提取代码来源为 [MaaFramework](https://github.com/MaaXYZ/MaaFramework)，见随包提供的 `LICENSE`。
