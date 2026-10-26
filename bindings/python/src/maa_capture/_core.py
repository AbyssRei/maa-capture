"""ABI 2 manager. Every returned frame owns copied bytes and frozen metadata."""
from __future__ import annotations
import ctypes as C
import logging
from . import _ffi as F, _loader
from ._types import *
MaaWSBool = C.c_uint8
_BOOL = C.CFUNCTYPE(MaaWSBool, C.c_void_p)
_CAPTURE = C.CFUNCTYPE(MaaWSBool, C.c_void_p, C.POINTER(F.MaaWSFrameInput), C.POINTER(F.MaaWSError))
_VOID = C.CFUNCTYPE(None, C.c_void_p)
class _Callbacks(C.Structure):
    _fields_ = [("struct_size", C.c_uint32), ("reserved", C.c_uint32), ("connect", _BOOL),
                ("screencap", _CAPTURE), ("release_frame", _VOID), ("inactive", _VOID)]

def setup(lib):
    pointer = C.c_void_p
    prototypes = {
        "MaaWSGetRuntimeInfo": (MaaWSBool,[C.POINTER(F.MaaWSRuntimeInfo)]),
        "MaaWSGetApiFeatures": (C.c_uint64,[]),
        "MaaWSManagerGetAvailableMethods": (C.c_uint64,[]),
        "MaaWSGetMethodCapabilities": (MaaWSBool,[C.c_uint64,C.POINTER(F.MaaWSMethodCapabilities),C.POINTER(F.MaaWSError)]),
        "MaaWSMonitorSnapshotCreate": (pointer,[C.POINTER(F.MaaWSError)]),
        "MaaWSMonitorSnapshotDestroy": (None,[pointer]),
        "MaaWSMonitorSnapshotGetCount": (MaaWSBool,[pointer,C.POINTER(C.c_uint32),C.POINTER(C.c_uint64)]),
        "MaaWSMonitorSnapshotGetInfo": (MaaWSBool,[pointer,C.c_uint32,C.POINTER(F.MaaWSMonitorInfo)]),
        "MaaWSManagerCreate": (pointer,[C.POINTER(F.MaaWSCaptureOptions),C.POINTER(F.MaaWSError)]),
        "MaaWSManagerDestroy": (None,[pointer]),
        "MaaWSManagerRegisterCustomUnit": (MaaWSBool,[pointer,C.c_char_p,C.c_uint32,C.c_uint32,C.POINTER(_Callbacks),pointer]),
        "MaaWSManagerConnect": (MaaWSBool,[pointer]),
        "MaaWSManagerScreencap": (MaaWSBool,[pointer,C.POINTER(pointer)]),
        "MaaWSManagerGetLastError": (MaaWSBool,[pointer,C.POINTER(F.MaaWSError)]),
        "MaaWSManagerSelectedUnitName": (C.c_char_p,[pointer]),
        "MaaWSManagerInactive": (None,[pointer]),
        "MaaWSFrameGetImage": (MaaWSBool,[pointer,C.POINTER(F.MaaWSImageView)]),
        "MaaWSFrameGetInfo": (MaaWSBool,[pointer,C.POINTER(F.MaaWSFrameInfo)]),
        "MaaWSFrameDestroy": (None,[pointer]),
    }
    for name,(result,args) in prototypes.items():
        func = getattr(lib,name)
        func.restype,func.argtypes = result,args

def _new(kind):
    value = kind()
    value.struct_size = C.sizeof(value)
    return value

def available_methods():
    return _loader.load().MaaWSManagerGetAvailableMethods()

def runtime_info():
    out = _new(F.MaaWSRuntimeInfo)
    if not _loader.load().MaaWSGetRuntimeInfo(C.byref(out)):
        raise RuntimeError("Runtime info query failed")
    return from_native(out,RuntimeInfo)

def method_capabilities(method):
    out,e = _new(F.MaaWSMethodCapabilities),_new(F.MaaWSError)
    if not _loader.load().MaaWSGetMethodCapabilities(integer(method),C.byref(out),C.byref(e)):
        raise CaptureException(from_native(e,CaptureError))
    return from_native(out,MethodCapabilities)

def monitors():
    lib = _loader.load()
    e = _new(F.MaaWSError)
    snapshot = lib.MaaWSMonitorSnapshotCreate(C.byref(e))
    if not snapshot:
        raise CaptureException(from_native(e,CaptureError))
    try:
        count,token = C.c_uint32(),C.c_uint64()
        if not lib.MaaWSMonitorSnapshotGetCount(snapshot,C.byref(count),C.byref(token)):
            raise RuntimeError("Snapshot count query failed")
        items = []
        for index in range(count.value):
            item = _new(F.MaaWSMonitorInfo)
            if not lib.MaaWSMonitorSnapshotGetInfo(snapshot,index,C.byref(item)):
                raise RuntimeError("Snapshot item query failed")
            items.append(from_native(item,MonitorInfo))
        return MonitorSnapshot(tuple(items),token.value)
    finally:
        lib.MaaWSMonitorSnapshotDestroy(snapshot)

class ScreencapCallbacks:
    def __init__(self, screencap, *, connect=None, inactive=None):
        self._pixels = None
        def safe(fn, fallback):
            try:
                return fn()
            except Exception:
                logging.getLogger(__name__).exception("Capture callback failed")
                return fallback
        def capture(_ctx, out, error):
            def invoke():
                frame = screencap()
                if frame is None:
                    return 0
                image = frame.image
                self._pixels = C.create_string_buffer(image.data)
                native = _new(F.MaaWSFrameInput)
                native.image = F.MaaWSImageView(C.sizeof(F.MaaWSImageView),integer(image.width,32),
                    integer(image.height,32),integer(image.channels,32),integer(image.step),len(image.data),C.addressof(self._pixels))
                native.info = to_native(frame.source,F.MaaWSFrameInfo)
                out[0] = native
                return 1
            return safe(invoke,0)
        def release(_ctx):
            self._pixels = None
        self._connect = _BOOL(lambda _: safe(lambda: bool(connect()) if connect else True,False))
        self._capture = _CAPTURE(capture)
        self._release = _VOID(release)
        self._inactive = _VOID(lambda _: safe(lambda: inactive() if inactive else None,None))
        self._native = _Callbacks(C.sizeof(_Callbacks),0,self._connect,self._capture,self._release,self._inactive)

class Manager:
    METHOD_NONE = 0
    METHOD_AUTO = 0
    METHOD_GDI = 1
    METHOD_FRAMEPOOL = 2
    METHOD_DXGI_DESKTOP_DUP = 4
    METHOD_DXGI_DESKTOP_DUP_WINDOW = 8
    METHOD_PRINTWINDOW = 16
    METHOD_SCREENDC = 32
    METHOD_PIPEWIRE = 64
    METHOD_WLR = 128
    METHOD_SCREENCAPTUREKIT = 256
    def __init__(self, *, target: CaptureTarget, methods: int = 0):
        self._handle = None
        self._callbacks = []
        options = _new(F.MaaWSCaptureOptions)
        options.target = target_native(target)
        options.methods = integer(methods)
        self._lib = _loader.load()
        e = _new(F.MaaWSError)
        self._handle = self._lib.MaaWSManagerCreate(C.byref(options),C.byref(e))
        if not self._handle:
            raise CaptureException(from_native(e,CaptureError))
        self.target = target
    @property
    def closed(self):
        return not self._handle
    def _require_handle(self):
        if self.closed:
            raise RuntimeError("Manager is closed")
        return self._handle
    def register_custom_unit(self, source_key, callbacks, *, scope=None, valid_fields=0):
        scope = self.target.scope if scope is None and isinstance(self.target,CustomTarget) else scope
        ok = self._lib.MaaWSManagerRegisterCustomUnit(self._require_handle(),text(source_key,512),integer(scope,32),integer(valid_fields,32),C.byref(callbacks._native),None)
        if ok:
            self._callbacks.append(callbacks)
        return bool(ok)
    def connect(self):
        return bool(self._lib.MaaWSManagerConnect(self._require_handle()))
    def last_error(self):
        e = _new(F.MaaWSError)
        if not self._lib.MaaWSManagerGetLastError(self._require_handle(),C.byref(e)):
            raise RuntimeError("Last error query failed")
        return from_native(e,CaptureError)
    def screencap(self):
        frame = C.c_void_p()
        if not self._lib.MaaWSManagerScreencap(self._require_handle(),C.byref(frame)):
            return None
        try:
            image,info = _new(F.MaaWSImageView),_new(F.MaaWSFrameInfo)
            if not self._lib.MaaWSFrameGetImage(frame,C.byref(image)) or not self._lib.MaaWSFrameGetInfo(frame,C.byref(info)):
                raise RuntimeError("Invalid native frame")
            return CaptureFrame(Image(image.width,image.height,image.channels,image.step,C.string_at(image.data,image.data_size)),from_native(info,SourceInfo))
        finally:
            self._lib.MaaWSFrameDestroy(frame)
    def selected_unit_name(self):
        value = self._lib.MaaWSManagerSelectedUnitName(self._require_handle())
        return value.decode("utf-8") if value else None
    def inactive(self):
        self._lib.MaaWSManagerInactive(self._require_handle())
    def close(self):
        handle = getattr(self,"_handle",None)
        if handle:
            self._handle = None
            try:
                self._lib.MaaWSManagerDestroy(handle)
            finally:
                self._callbacks.clear()
    def __enter__(self):
        self._require_handle()
        return self
    def __exit__(self,*args):
        self.close()
        return False
    def __del__(self):
        try:
            self.close()
        except Exception:
            pass