"""Windows 窗口工具（仅 Windows 平台，由包 ``__init__`` 条件导出）。"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
from typing import Optional


def find_window_by_title(title: str) -> Optional[int]:
    """按窗口标题查找顶层窗口句柄（不区分大小写，模糊匹配）。"""
    _user32 = ctypes.windll.user32

    results = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def enum_proc(hwnd, lparam):
        length = _user32.GetWindowTextLengthW(hwnd)
        if length > 0:
            buf = ctypes.create_unicode_buffer(length + 1)
            _user32.GetWindowTextW(hwnd, buf, length + 1)
            if title.lower() in buf.value.lower():
                results.append(hwnd)
        return True

    _user32.EnumWindows(enum_proc, 0)
    return results[0] if results else None


def foreground_window() -> Optional[int]:
    """当前前台窗口句柄（无窗口返回 None）。"""
    hwnd = ctypes.windll.user32.GetForegroundWindow()
    return int(hwnd) if hwnd else None
