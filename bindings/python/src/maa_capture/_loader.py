"""原生库定位与加载（跨平台）。

加载顺序：
  1. set_lib_path() 显式指定的文件或目录
  2. 环境变量 ``MAA_WS_LIB``（推荐）或 ``MAA_WS_DLL_DIR``（旧名，等价），
     值为库文件或所在目录
  3. 随 wheel 分发的内置原生库 ``_native/<platform>/<libname>``
  4. 系统搜索（ctypes.CDLL 按名加载，即 PATH / ldconfig / DYLD 路径）

任一步骤命中即缓存；全部失败时抛 RuntimeError 并汇总各路径的失败原因。
"""

from __future__ import annotations

import ctypes
import os
import sys
from typing import Optional

# 各平台原生库文件名与 wheel 内置目录的映射（与 CI 产物布局一致）
if sys.platform == "win32":
    _LIB_NAME = "maa-capture.dll"
    _BUNDLED_DIR = "windows-x64"
elif sys.platform == "darwin":
    _LIB_NAME = "libmaa-capture.dylib"
    _BUNDLED_DIR = "macos-arm64"
else:
    _LIB_NAME = "libmaa-capture.so"
    _BUNDLED_DIR = "linux-x64"

_override: Optional[str] = None
_lib: Optional[ctypes.CDLL] = None


def set_lib_path(path: str) -> None:
    """指定原生库的文件路径或所在目录（下次使用时重新加载）。"""
    global _override, _lib
    _override = path or None
    _lib = None


def lib_path() -> Optional[str]:
    """返回当前显式指定的库路径（未设置为 None）。"""
    return _override


# 旧 API 别名（早期绑定仅支持 Windows DLL 时使用）
set_dll_path = set_lib_path
dll_path = lib_path


def _candidates() -> list[str]:
    here = os.path.dirname(os.path.abspath(__file__))
    out = []

    def add(p: Optional[str]) -> None:
        if not p:
            return
        if os.path.isdir(p):
            out.append(os.path.join(p, _LIB_NAME))
        else:
            out.append(p)

    add(_override)
    add(os.environ.get("MAA_WS_LIB"))
    add(os.environ.get("MAA_WS_DLL_DIR"))
    # 随包分发的内置原生库
    out.append(os.path.join(here, "_native", _BUNDLED_DIR, _LIB_NAME))
    return out


def load() -> ctypes.CDLL:
    """加载并缓存原生库；供 Manager / 回调接口内部使用。"""
    global _lib
    if _lib is not None:
        return _lib

    errors = []
    for path in _candidates():
        if os.path.exists(path):
            try:
                _lib = ctypes.CDLL(path)
                return _lib
            except OSError as e:  # pragma: no cover
                errors.append(f"{path}: {e}")
        else:
            errors.append(f"{path}: not found")

    # 兜底：交给系统动态库搜索
    try:
        _lib = ctypes.CDLL(_LIB_NAME)
        return _lib
    except OSError as e:  # pragma: no cover
        errors.append(f"system search ({_LIB_NAME}): {e}")

    raise RuntimeError(
        f"无法加载 {_LIB_NAME}。可调用 set_lib_path() 指定路径，"
        f"或设置 MAA_WS_LIB 环境变量，或将库放入系统搜索路径。"
        f"尝试过的位置:\n" + "\n".join(errors)
    )
