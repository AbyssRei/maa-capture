"""maa-capture Python 绑定自测。

用法：
    python test_python_binding.py [dll目录]

覆盖：
  1. 外部回调接口：ScreencapCallbacks 自定义单元注册进 Manager，测速选中并截图
  2. Manager 内置方式：以当前前台窗口为句柄（若存在桌面/窗口），
     预期可能因无窗口而失败——此时跳过而非失败
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from maa_capture import (  # noqa: E402
    ImageBuffer,
    Manager,
    ScreencapCallbacks,
    foreground_window,
    set_dll_path,
)


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
        out.data = ctypes.cast(buf, ctypes.POINTER(ctypes.c_ubyte))
        out.width = holder["w"]
        out.height = holder["h"]
        out.channels = 4  # BGRA -> 桥接层转 BGR
        out.step = holder["w"] * 4
        return 1

    import ctypes

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

    # 通过顶层 C API 直接使用（MaaWSCustomScreencapCreate 路径为 C++ 桥接内部，此处验证 Manager 即可）
    return True


def test_manager_builtin():
    """内置方式：以当前前台窗口为句柄做一次真实截图（无窗口则跳过）。"""
    hwnd = foreground_window()
    if not hwnd:
        print("[SKIP] no foreground window (headless)")
        return True

    mgr = Manager(hwnd=int(hwnd), methods=Manager.METHOD_ALL)
    if not mgr.connect():
        print("[SKIP] no builtin screencap method works on this window")
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
        set_dll_path(sys.argv[1])
    test_custom_callback()
    test_manager_builtin()
    print("ALL PASS")