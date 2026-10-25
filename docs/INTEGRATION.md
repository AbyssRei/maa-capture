# 接口、图像与生命周期

主仓库：[AbyssRei/maa-capture](https://github.com/AbyssRei/maa-capture)。外部接口示例在 [sample](../sample/)，语言绑定入口在 [bindings](../bindings/README.md)。

## 两种扩展入口

- C++：继承 `mws::ScreencapBase` 并实现 `screencap()`，通过 `ScreencapMgr::register_unit()` 注册。
- C ABI：填充 `MaaWSCustomScreencapCallbacks`，通过 `MaaWSManagerRegisterCustomUnit()` 注册，或使用单独的 `MaaWSCustomScreencapCreate()` 句柄。

自定义单元与启用的内置方式一起参与测速。仅用自定义来源时传 `Method_None` / `MAA_WS_METHOD_NONE` / `Manager.METHOD_NONE`。

## 推荐调用顺序

1. 创建管理器并选择内置方式。
2. 在首次 `connect()` 前配置窗口、显示器、PipeWire 目标或 Wayland socket，注册自定义单元。
3. 检查 `connect()` 是否成功，再循环调用 `screencap()`。
4. 不再需要画面时调用 `inactive()`；程序结束或异常退出时销毁管理器。

`inactive()` 用于停用生产者或恢复临时窗口状态，不等于销毁句柄。再次截图时后端会按需恢复。更换来源或重新建立 Portal 授权时，建议创建新管理器并使用新的连接资源。

Python 可使用 `with Manager(...) as capture:`，也可显式 `close()`。`close()` 可重复调用，关闭后继续操作会抛出 RuntimeError；异常退出 `with` 也会执行清理，原异常继续传播。

## 图像格式与所有权

输出为 BGR 三通道，字段包括宽、高、通道数及每行字节数 `step`。不要默认把任意原始缓冲按 `width * height * channels` 连续解释；原始回调可以带行填充。

C ABI 返回的图像指针由句柄持有，有效期到下一次截图或句柄销毁前。不要自行释放这个指针，需长期保留时立即复制。Python 的 `Manager.screencap()` 返回 `ImageBuffer.Info`，其中 `data` 是已复制的 bytes，可独立保留。

C++ `Image` 的拷贝共享底层所有权，`clone()` 深拷贝；包装外部缓冲的 `Image::wrap()` 不负责延长外部缓冲的生命周期。

## 回调内存必须保持有效

C 侧保存回调结构体及函数指针。调用方必须确保结构体、函数对象、上下文及像素缓冲在使用期间存活，不能把返回后即失效的栈上像素缓冲交给原生层。

Python 示例使用持久 ctypes 缓冲：

```python
import ctypes
from maa_capture import ImageBuffer, Manager, ScreencapCallbacks

pixels = (ctypes.c_ubyte * 8)(10, 20, 30, 255, 40, 50, 60, 255)
def capture_frame(_arg, out: ImageBuffer):
    out.data = ctypes.cast(pixels, ctypes.POINTER(ctypes.c_ubyte))
    out.width, out.height, out.channels, out.step = 2, 1, 4, 8
    return 1

callbacks = ScreencapCallbacks(screencap=capture_frame)
with Manager(methods=Manager.METHOD_NONE) as capture:
    assert capture.register_custom_unit("example", callbacks)
    assert capture.connect()
    image = capture.screencap()
    assert image.data == bytes([10, 20, 30, 40, 50, 60])
```

回调应返回 1 表示成功、0 表示失败，内部异常应转成失败结果。测速会调用截图回调多次，也会停用未选中的候选；`inactive` 回调应可重复调用，并允许未来截图时恢复来源。

## 线程与 API 边界

同一管理器的配置、连接、截图、停用和关闭操作需要调用方串行化。Python 的 GIL 不能作为原生对象线程安全保证。多线程场景可使用独立管理器或调用方锁。

PipeWire 内部的流线程不改变上述约束。回调里不要并发关闭正在使用的管理器，也不要假设截图回调只执行一次。

`hwnd` 在 Windows 是 HWND，在 macOS 是 CGWindowID 的整数值，在 Linux 被忽略。窗口枚举、Portal 授权、键鼠输入和 OCR 不由这些截图接口提供。

## C ABI 兼容

C ABI 保留 `MaaWS` 命名，结构字段使用固定宽度整数和指针。C++ 需要使用匹配的编译器 ABI；跨语言或跨编译器集成优先使用 C ABI。

Windows 的专用 C++ 头文件位于 `maa-capture/Windows/`；跨平台入口位于 `maa-capture/`，见[构建接入](BUILDING.md)。
