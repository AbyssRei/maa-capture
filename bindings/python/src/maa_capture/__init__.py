"""Explicit targets, immutable snapshots and owned capture frames (ABI 2)."""
from ._types import (CaptureTarget,MonitorTarget,WindowTarget,PipeWireTarget,WlrTarget,CustomTarget,
    Rect,Image,SourceInfo,CaptureFrame,MonitorInfo,MonitorSnapshot,MethodCapabilities,RuntimeInfo,
    CaptureError,CaptureException,provider_frame)
from ._core import Manager,ScreencapCallbacks,available_methods,monitors,method_capabilities,runtime_info
from ._loader import set_lib_path,lib_path
__version__ = "1.0.0"
__all__ = [name for name in globals() if not name.startswith("_")]
__all__.append("__version__")
