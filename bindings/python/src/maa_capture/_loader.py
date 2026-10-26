"""Load a matching ABI 2 library before configuring any changed prototypes."""
from __future__ import annotations
import ctypes
import os
import platform
import sys
_override = None
_lib = None
_loaded_path = None
_LIB_NAME = "maa-capture.dll" if sys.platform == "win32" else "libmaa-capture.dylib" if sys.platform == "darwin" else "libmaa-capture.so"
_BUNDLED_DIR = {"win32":"windows-x64","darwin":"macos-arm64"}.get(sys.platform,"linux-x64")

def set_lib_path(path):
    global _override,_lib,_loaded_path
    _override = os.fspath(path) if path else None
    _lib = None
    _loaded_path = None

def lib_path():
    """Known actual location of the loaded library, or the explicit request before load."""
    return _loaded_path if _lib is not None else _override

def _validate(lib, path):
    from . import __version__
    native = "unknown"
    try:
        abi = lib.MaaWSGetAbiVersion
        abi.argtypes,abi.restype = [],ctypes.c_uint32
        generation = abi()
        if generation != 2:
            raise RuntimeError(f"expected ABI 2, got {generation}")
        from . import _ffi as F
        query = lib.MaaWSGetRuntimeInfo
        query.argtypes,query.restype = [ctypes.POINTER(F.MaaWSRuntimeInfo)],ctypes.c_uint8
        info = F.MaaWSRuntimeInfo()
        info.struct_size = ctypes.sizeof(info)
        if lib.MaaWSGetRuntimeInfo(ctypes.byref(info)):
            native = info.version.decode()
            if info.abi_version != 2:
                raise RuntimeError("runtime ABI disagrees with query")
        else:
            raise RuntimeError("native runtime query failed")
        from ._core import setup
        setup(lib)
    except (AttributeError,RuntimeError) as error:
        raise RuntimeError(f"maa-capture package={__version__}, native={native}, path={path or 'unknown (system search)'}: {error}") from error

def load():
    global _lib,_loaded_path
    if _lib is not None:
        return _lib
    machine = platform.machine().lower()
    supported = (sys.platform == "darwin" and machine in ("arm64","aarch64")) or (sys.platform in ("win32","linux") and machine in ("amd64","x86_64"))
    if not supported and not (_override or os.environ.get("MAA_WS_LIB")):
        raise RuntimeError(f"No bundled native library for {sys.platform}/{machine}; specify a matching build via set_lib_path")
    explicit = _override or os.environ.get("MAA_WS_LIB")
    candidates = [explicit] if explicit else [os.path.join(os.path.dirname(__file__),"_native",_BUNDLED_DIR,_LIB_NAME),_LIB_NAME]
    failures = []
    for candidate in candidates:
        path = os.path.join(candidate,_LIB_NAME) if os.path.isdir(candidate) else candidate
        try:
            lib = ctypes.CDLL(path)
        except OSError as error:
            failures.append(f"{path}: {error}")
            continue
        known = os.path.abspath(path) if os.path.isfile(path) else None
        _validate(lib,known)  # A loaded mismatch is fatal, never masked by a fallback.
        if sys.platform == "win32":
            query = ctypes.WinDLL("kernel32",use_last_error=True).GetModuleFileNameW
            query.argtypes,query.restype = [ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_uint32],ctypes.c_uint32
            value = ctypes.create_unicode_buffer(32768)
            if 0 < query(lib._handle,value,len(value)) < len(value):
                known = os.path.realpath(value.value)
        _lib,_loaded_path = lib,known
        return lib
    from . import __version__
    raise RuntimeError(f"Cannot load maa-capture package={__version__}, native=unknown:\n" + "\n".join(failures))
