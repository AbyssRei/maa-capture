"""Explicit targets, immutable snapshots and owned capture frames (ABI 2)."""
from ._types import (CaptureTarget,MonitorTarget,WindowTarget,PipeWireTarget,WlrTarget,CustomTarget,
    Rect,Image,SourceInfo,FrameSampleInfo,CaptureFrame,MonitorInfo,MonitorSnapshot,MethodCapabilities,MethodBehavior,CapturePolicy,RuntimeInfo,
    CaptureError,CaptureException,provider_frame)
from ._core import Manager,Cancellation,ScreencapCallbacks,available_methods,monitors,method_capabilities,method_behavior,runtime_info
from ._loader import set_lib_path,lib_path
__version__ = "3.0.0"
__all__ = [name for name in globals() if not name.startswith("_")]
__all__.append("__version__")
