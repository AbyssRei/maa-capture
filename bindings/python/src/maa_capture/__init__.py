"""maa-capture —— maa-capture 的 Python 绑定（ctypes，零编译依赖）。

对应 MaaFramework 的 Python 绑定角色，封装两个外部扩展接口：
  1. ScreencapCallbacks —— 外部回调实现截图（对应 MaaCustomControllerCallbacks::screencap）
  2. Manager            —— 当前平台内置截图方式的测速选择 + 外部单元注册

用法：
    from maa_capture import Manager, ScreencapCallbacks, ImageBuffer

    # 方式一：外部回调实现截图（全平台）
    def my_screencap(arg, out: ImageBuffer) -> int:
        # 把像素写入 out（BGRA/BGR），返回 1
        ...
    cb = ScreencapCallbacks(screencap=my_screencap, trans_arg=None)
    mgr = Manager(hwnd=None, methods=Manager.METHOD_NONE)
    mgr.register_custom_unit("my-unit", cb)
    mgr.connect()
    img = mgr.screencap()   # ImageBuffer.Info
    print(mgr.selected_unit_name())

    # 方式二：内置方式
    #   Windows：mgr = Manager(hwnd=find_window_by_title("记事本"))
    #   三平台整屏：mgr = Manager()  # macOS 截主显示器；Linux 需先配置 PipeWire/Wlr 目标
    mgr.connect()
    img = mgr.screencap()

依赖：仅标准库 ctypes；可选 numpy（``pip install maa-capture[numpy]``）。
"""

from __future__ import annotations

import sys

from ._core import ImageBuffer, MaaWSBool, Manager, ScreencapCallbacks
from ._loader import lib_path, set_lib_path

__version__ = "0.2.0"

__all__ = [
    "ImageBuffer",
    "ScreencapCallbacks",
    "Manager",
    "MaaWSBool",
    "set_lib_path",
    "lib_path",
    "set_dll_path",
    "dll_path",
    "__version__",
]

# 旧 API（v1.0 前的 Windows 专用命名）保留为别名
set_dll_path = set_lib_path
dll_path = lib_path

if sys.platform == "win32":
    from ._win32 import find_window_by_title, foreground_window

    __all__ += ["find_window_by_title", "foreground_window"]
