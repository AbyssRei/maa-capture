"""maa-capture Python 绑定自测。

用法（需先安装包，例如 ``pip install -e bindings/python`` 或安装 wheel）：
    python tests/test_binding.py [原生库目录]

覆盖：
  1. 外部回调接口：ScreencapCallbacks 自定义单元注册进 Manager，测速选中并截图
  2. Manager 内置方式：真实截图一次（无可用桌面/窗口的环境自动跳过）
"""

from __future__ import annotations

import ctypes
import os
import sys
import unittest

from maa_capture import ImageBuffer, Manager, ScreencapCallbacks, available_methods, set_lib_path


def test_query_helpers():
    """查询辅助：可用方式位掩码 + 显示器枚举。"""
    methods = available_methods()
    assert methods != 0, f"available_methods returned {methods:#x}"

    mgr = Manager(hwnd=None, methods=Manager.METHOD_NONE)
    count = mgr.monitor_count()
    if count == 0:
        # 无显示器信息的环境（Linux 无 Wayland / macOS 无 TCC 权限）
        print(f"[PASS] query helpers: methods={methods:#x}, monitors=0 (unavailable)")
        return True

    info0 = mgr.monitor_info(0)
    assert info0 is not None, "monitor_info(0) returned None"
    assert isinstance(info0["primary"], bool) and isinstance(info0["name"], str)
    if sys.platform == "win32":
        assert info0["primary"], "Windows 索引 0 应为主显示器"
    assert mgr.monitor_info(count) is None, "越界索引应返回 None"

    names = [mgr.monitor_info(i)["name"] for i in range(count)]
    print(f"[PASS] query helpers: methods={methods:#x}, monitors={count} {names}")
    return True




def test_callback_exception():
    def fail(_arg, _out):
        raise ValueError("callback failure")
    with Manager(methods=Manager.METHOD_NONE) as manager:
        assert manager.register_custom_unit("failure", ScreencapCallbacks(screencap=fail))
        with unittest.TestCase().assertLogs("maa_capture._core", level="ERROR"):
            assert not manager.connect()
    print("[PASS] callback exceptions become a capture failure")


def test_manager_lifecycle():
    """Context exit releases native callbacks even while propagating an exception."""
    calls = []
    callbacks = ScreencapCallbacks(
        screencap=lambda _arg, _out: 0,
        inactive=lambda _arg: calls.append("inactive") or 1,
    )
    mgr = Manager(methods=Manager.METHOD_NONE)
    try:
        with mgr as active:
            assert active is mgr and not mgr.closed
            assert mgr.register_custom_unit("lifecycle", callbacks)
            raise ValueError("sentinel")
    except ValueError as error:
        assert str(error) == "sentinel"
    else:
        raise AssertionError("Context manager swallowed the exception")
    assert mgr.closed and calls == ["inactive"]
    mgr.close()
    assert calls == ["inactive"]
    try:
        mgr.connect()
    except RuntimeError as error:
        assert "closed" in str(error)
    else:
        raise AssertionError("Closed manager accepted an operation")
    print("[PASS] deterministic manager lifecycle")


def make_gradient(width=8, height=6):
    """生成 BGRA 渐变像素。"""
    px = bytearray(width * height * 4)
    i = 0
    for y in range(height):
        for x in range(width):
            px[i] = x * 255 // width  # B
            px[i + 1] = y * 255 // height  # G
            px[i + 2] = 128  # R
            px[i + 3] = 255  # A
            i += 4
    return bytes(px), width, height


def test_custom_callback():
    """外部回调实现截图（自定义单元注册进 Manager）。"""
    pixels, w, h = make_gradient()
    holder = {"pixels": pixels, "w": w, "h": h}

    def on_screencap(_arg, out: ImageBuffer) -> int:
        data = holder["pixels"]
        buf = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
        holder["buffer"] = buf  # Keep pixels alive until the native caller copies them.
        out.data = ctypes.cast(buf, ctypes.POINTER(ctypes.c_ubyte))
        out.width = holder["w"]
        out.height = holder["h"]
        out.channels = 4  # BGRA -> 桥接层转 BGR
        out.step = holder["w"] * 4
        return 1

    cb = ScreencapCallbacks(screencap=on_screencap)
    mgr = Manager(hwnd=None, methods=Manager.METHOD_NONE)
    assert mgr.register_custom_unit("python-gradient", cb), "register_custom_unit failed"
    assert mgr.connect(), "connect failed"
    assert mgr.selected_unit_name() == "python-gradient", mgr.selected_unit_name()

    info = mgr.screencap()
    assert info is not None, "screencap returned None"
    assert (info.width, info.height) == (w, h), (info.width, info.height)
    assert info.channels == 3, f"expected normalized BGR 3ch, got {info.channels}"
    # 首像素 BGR = (0, 0, 128)
    assert info.data[:3] == bytes([0, 0, 128]), info.data[:6]
    mgr.inactive()
    del mgr
    print("[PASS] custom callback unit")

    return True


def test_manager_builtin():
    """内置方式：真实截图一次（无可用桌面/输出则跳过）。"""
    if sys.platform == "win32":
        from maa_capture import foreground_window

        hwnd = foreground_window()
        if not hwnd:
            print("[SKIP] no foreground window (headless)")
            return True
        mgr = Manager(hwnd=int(hwnd), methods=Manager.METHOD_ALL)
    else:
        # Linux/macOS：hwnd 语义由平台解释（整屏/输出），CI 无桌面环境则 connect 失败跳过
        mgr = Manager(hwnd=None, methods=Manager.METHOD_ALL)

    if not mgr.connect():
        print("[SKIP] no builtin screencap method works in this environment")
        return True

    info = mgr.screencap()
    assert info is not None, "builtin screencap failed"
    assert info.width > 0 and info.height > 0, (info.width, info.height)
    assert info.channels == 3, info.channels
    print(f"[PASS] builtin manager: selected={mgr.selected_unit_name()} {info.width}x{info.height}")
    mgr.inactive()
    return True


if __name__ == "__main__":
    if len(sys.argv) > 1:
        set_lib_path(sys.argv[1])
    test_custom_callback()
    test_manager_lifecycle()
    test_callback_exception()
    test_query_helpers()
    test_manager_builtin()
    print("ALL PASS")
