"""Immutable public values; ctypes layouts live in _ffi, never in returned data."""
from __future__ import annotations
import ctypes as C
from dataclasses import dataclass, field, make_dataclass
from . import _ffi as F

def _value_type(name, native):
    fields = []
    for key, kind in native._fields_:
        if key in ("struct_size", "reserved"):
            continue
        if kind is F.MaaWSRect:
            fields.append((key, Rect, field(default_factory=Rect)))
        elif kind is F.MaaWSMonitorInfo:
            fields.append((key, MonitorInfo, field(default_factory=MonitorInfo)))
        elif issubclass(kind, C.Array):
            fields.append((key, str, ""))
        elif kind is C.c_double:
            fields.append((key, float, 0.0))
        else:
            fields.append((key, int, 0))
    return make_dataclass(name, fields, frozen=True, slots=True, namespace={"__module__": __name__})

Rect = _value_type("Rect", F.MaaWSRect)
CaptureError = _value_type("CaptureError", F.MaaWSError)
MonitorInfo = _value_type("MonitorInfo", F.MaaWSMonitorInfo)
SourceInfo = _value_type("SourceInfo", F.MaaWSFrameInfo)
MethodCapabilities = _value_type("MethodCapabilities", F.MaaWSMethodCapabilities)
RuntimeInfo = _value_type("RuntimeInfo", F.MaaWSRuntimeInfo)
FrameSampleInfo = _value_type("FrameSampleInfo", F.MaaWSFrameSampleInfo)
MethodBehavior = _value_type("MethodBehavior", F.MaaWSMethodBehavior)

@dataclass(frozen=True, slots=True)
class CapturePolicy:
    framepool_cache: bool = False
    dxgi_cache: bool = False
    strict_background: bool = False
    fresh_only: bool = False
    diagnostics: bool = False
    max_sample_age_ns: int = 0
    connect_timeout_ns: int = 0
    capture_timeout_ns: int = 0
    first_frame_timeout_ns: int = 0

def advanced_policy_native(policy, framepool_cache=False):
    if not isinstance(policy,CapturePolicy):
        raise TypeError("policy must be CapturePolicy")
    out = F.MaaWSAdvancedCapturePolicy()
    out.struct_size = C.sizeof(out)
    out.flags = int(framepool_cache)
    for name,bit in (("framepool_cache",1),("dxgi_cache",2),("strict_background",4),("fresh_only",8),("diagnostics",16)):
        value = getattr(policy,name)
        if not isinstance(value,bool):
            raise TypeError(f"{name} must be a bool")
        if value: out.flags |= bit
    for name in ("max_sample_age_ns","connect_timeout_ns","capture_timeout_ns","first_frame_timeout_ns"):
        setattr(out,name,integer(getattr(policy,name)))
    return out

def from_native(value, public):
    data = {}
    for key, kind in value._fields_:
        if key in ("struct_size", "reserved"):
            continue
        item = getattr(value, key)
        data[key] = from_native(item, Rect) if kind is F.MaaWSRect else from_native(item, MonitorInfo) if kind is F.MaaWSMonitorInfo else item.decode("utf-8", "strict") if issubclass(kind, C.Array) else item
    return public(**data)

def to_native(value, native):
    out = native()
    if hasattr(out, "struct_size"):
        out.struct_size = C.sizeof(native)
    for key, kind in native._fields_:
        if key in ("struct_size", "reserved"):
            continue
        item = getattr(value, key)
        if kind is F.MaaWSRect:
            item = to_native(item, kind)
        elif kind is F.MaaWSMonitorInfo:
            item = to_native(item, kind)
        elif issubclass(kind, C.Array):
            item = text(item, kind._length_)
        elif kind in (C.c_uint32,C.c_int32,C.c_uint64,C.c_int64):
            if key in ("primary","available") and isinstance(item,bool):
                item = int(item)
            item = integer(item,C.sizeof(kind)*8,kind in (C.c_int32,C.c_int64))
        setattr(out, key, item)
    return out

def integer(value, bits=64, signed=False):
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError("Expected an integer")
    low = -(1 << (bits - 1)) if signed else 0
    high = (1 << (bits - int(signed))) - 1
    if not low <= value <= high:
        raise ValueError(f"Integer outside {'signed' if signed else 'unsigned'} {bits}-bit range")
    return value

def text(value, capacity):
    if not isinstance(value, str) or "\0" in value:
        raise ValueError("Expected a UTF-8 string without NUL")
    encoded = value.encode("utf-8")
    if len(encoded) >= capacity:
        raise ValueError("UTF-8 string exceeds native capacity")
    return encoded

@dataclass(frozen=True, slots=True)
class MonitorTarget:
    monitor_key: str

@dataclass(frozen=True, slots=True)
class WindowTarget:
    window_id: int
    platform: int
    process_id: int = 0

@dataclass(frozen=True, slots=True)
class PipeWireTarget:
    fd: int
    node_id: int
    scope: int = 4

@dataclass(frozen=True, slots=True)
class WlrTarget:
    socket_name: str = ""
    output_index: int = 0

@dataclass(frozen=True, slots=True)
class CustomTarget:
    source_key: str
    scope: int = 8

CaptureTarget = MonitorTarget | WindowTarget | PipeWireTarget | WlrTarget | CustomTarget

def target_native(target):
    out = F.MaaWSCaptureTarget()
    out.struct_size = C.sizeof(out)
    if isinstance(target, MonitorTarget):
        out.type = 1
        out.source_key = text(target.monitor_key, 512)
        if not target.monitor_key:
            raise ValueError("Monitor key is required")
    elif isinstance(target, WindowTarget):
        out.type = 2
        out.window_id = integer(target.window_id)
        out.platform = integer(target.platform, 32)
        out.process_id = integer(target.process_id)
        if not out.window_id or out.platform not in (1, 2):
            raise ValueError("Window requires a nonzero ID and Windows(1)/macOS(2) platform")
    elif isinstance(target, PipeWireTarget):
        out.type, out.platform = 3, 4
        out.pipewire_fd = integer(target.fd, 32, True)
        out.node_id = integer(target.node_id, 32)
        out.scope = integer(target.scope, 32)
    elif isinstance(target, WlrTarget):
        out.type, out.platform = 4, 4
        out.socket_name = text(target.socket_name, 256)
        out.output_index = integer(target.output_index, 32)
    elif isinstance(target, CustomTarget):
        out.type = 5
        out.source_key = text(target.source_key, 512)
        out.scope = integer(target.scope, 32)
        if not target.source_key:
            raise ValueError("Custom source key is required")
    else:
        raise TypeError("An explicit typed capture target is required")
    return out

@dataclass(frozen=True, slots=True)
class Image:
    width: int
    height: int
    channels: int
    step: int
    data: bytes

@dataclass(frozen=True, slots=True)
class CaptureFrame:
    image: Image
    source: SourceInfo
    sample: FrameSampleInfo = field(default_factory=FrameSampleInfo)

@dataclass(frozen=True, slots=True)
class MonitorSnapshot:
    items: tuple[MonitorInfo, ...]
    layout_token: int

class CaptureException(RuntimeError):
    def __init__(self, error: CaptureError):
        self.error = error
        super().__init__(f"{error.message} (code={error.code}, stage={error.stage}, native={error.native_domain}:{error.native_code})")

def provider_frame(*, source_key: str, data: bytes, width: int, height: int, channels: int = 4, step: int | None = None, scope: int = 8) -> CaptureFrame:
    """Declare provider pixels without claiming OS-verified monitor identity."""
    width, height = integer(width, 32), integer(height, 32)
    if channels not in (3, 4) or not width or not height:
        raise ValueError("Expected nonempty BGR/BGRA pixels")
    stride = integer(step if step is not None else width * channels)
    if stride < width * channels or len(data) < (height - 1) * stride + width * channels:
        raise ValueError("Pixel data or stride is too short")
    return CaptureFrame(Image(width,height,channels,stride,bytes(data)),
        SourceInfo(target_type=5,scope=scope,source_key=source_key,pixel_width=width,pixel_height=height,
                   coordinate_unit=3,coordinate_space=2,axis=1,pixel_semantics=2,verification=1,limitations=4))
