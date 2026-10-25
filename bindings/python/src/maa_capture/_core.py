"""ctypes 绑定主体：ImageBuffer / ScreencapCallbacks / Manager。

对应纯 C 头 ``MaaWSCustomScreencap.h`` 的 Manager C API（跨平台）：
  - Windows：GDI / ScreenDC / PrintWindow / DXGI×2 / FramePool
  - Linux  ：PipeWire / Wlr（目标经 set_pipewire_target / set_wlr_socket 配置）
  - macOS  ：ScreenCaptureKit（hwnd 传 None 即 CGWindowID 0 = 整个主显示器）
"""

from __future__ import annotations

import ctypes
from dataclasses import dataclass
from typing import Callable, Optional

from . import _loader

# ---------------------------------------------------------------------------
# 数据类型（与 MaaWSCustomScreencap.h 对齐）
# ---------------------------------------------------------------------------

MaaWSBool = ctypes.c_ubyte


class ImageBuffer(ctypes.Structure):
    """MaaWSImageBuffer：像素（BGRA/BGR）+ 尺寸 + 步长。"""

    _fields_ = [
        ("data", ctypes.POINTER(ctypes.c_ubyte)),
        ("width", ctypes.c_int32),
        ("height", ctypes.c_int32),
        ("channels", ctypes.c_int32),
        ("step", ctypes.c_int32),
    ]

    def to_bytes(self) -> bytes:
        """复制像素数据为 bytes（按 step 行序）。"""
        if not self.data or self.width <= 0 or self.height <= 0:
            return b""
        row = self.step if self.step > 0 else self.width * self.channels
        return ctypes.string_at(self.data, row * self.height)

    def to_numpy(self):
        """转换为 numpy 数组（BGR）。需已安装 numpy（extras：pip install maa-capture[numpy]）。"""
        import numpy as np

        arr = np.frombuffer(self.to_bytes(), dtype=np.uint8)
        return arr.reshape(self.height, self.step)[:, : self.width * self.channels].reshape(
            self.height, self.width, self.channels
        )

    @dataclass
    class Info:
        width: int
        height: int
        channels: int
        step: int
        data: bytes


# 回调原型。
# 注意：ctypes 回调收到 POINTER(Structure) 参数时转换成的 Structure 是【副本】，
# 对其字段的修改不会写回原生内存。因此 screencap 回调一律用裸指针 (c_void_p)，
# 由包装层通过 ImageBuffer.from_address 构造"指针背书"的结构，写入才能持久。
_screencap_cb_type = ctypes.CFUNCTYPE(MaaWSBool, ctypes.c_void_p, ctypes.c_void_p)
_bool_cb_type = ctypes.CFUNCTYPE(MaaWSBool, ctypes.c_void_p)


class ScreencapCallbacks:
    """
    C 回调封装（MaaWSCustomScreencapCallbacks）。
    screencap(arg, out) -> int：把图像写入 out（推荐 4 通道 BGRA），成功返回 1。
    connect/connected/inactive 可选，返回 1/0。
    """

    def __init__(
        self,
        screencap: Callable[[Optional[object], ImageBuffer], int],
        trans_arg: Optional[object] = None,
        connect: Optional[Callable[[Optional[object]], int]] = None,
        connected: Optional[Callable[[Optional[object]], int]] = None,
        inactive: Optional[Callable[[Optional[object]], int]] = None,
    ):
        self._trans_arg = trans_arg
        self._screencap_cb = self._make_screencap_wrapper(screencap) if screencap else None
        self._connect_cb = _bool_cb_type(connect) if connect else None
        self._connected_cb = _bool_cb_type(connected) if connected else None
        self._inactive_cb = _bool_cb_type(inactive) if inactive else None

    def _make_screencap_wrapper(self, user_fn):
        @_screencap_cb_type
        def wrapper(trans_arg, buf_ptr):
            if not buf_ptr:
                return 0
            try:
                out = ImageBuffer.from_address(buf_ptr)
            except (ValueError, TypeError):
                return 0
            return 1 if user_fn(trans_arg, out) else 0

        wrapper._user_fn = user_fn  # 保活引用（防 GC 释放用户函数）
        return wrapper

    def _ctor(self) -> ctypes.Structure:
        class Struct(ctypes.Structure):
            _fields_ = [
                ("connect", _bool_cb_type),
                ("connected", _bool_cb_type),
                ("screencap", _screencap_cb_type),
                ("inactive", _bool_cb_type),
            ]

        s = Struct()
        s.connect = self._connect_cb or _bool_cb_type()
        s.connected = self._connected_cb or _bool_cb_type()
        s.screencap = self._screencap_cb
        s.inactive = self._inactive_cb or _bool_cb_type()
        return s


class Manager:
    """截图管理器（MaaWSManager*）：内置方式测速选择 + 外部回调单元注册。

    hwnd 的平台语义：
      - Windows：目标窗口 HWND（int）；为 None 时仅 DXGI_DesktopDup 可用（主显示器整屏）
      - Linux  ：忽略；PipeWire / Wlr 目标经 set_pipewire_target / set_wlr_socket 配置
      - macOS  ：目标窗口 CGWindowID；None 即 0 = 整个主显示器
    """

    # 截图方式位掩码（与 C 头文件常数一致）
    METHOD_NONE = 0
    METHOD_GDI = 1 << 0
    METHOD_FRAMEPOOL = 1 << 1
    METHOD_DXGI_DESKTOP_DUP = 1 << 2
    METHOD_DXGI_DESKTOP_DUP_WINDOW = 1 << 3
    METHOD_PRINTWINDOW = 1 << 4
    METHOD_SCREENDC = 1 << 5
    METHOD_PIPEWIRE = 1 << 6 # Linux
    METHOD_WLR = 1 << 7 # Linux
    METHOD_SCREENCAPTUREKIT = 1 << 8 # macOS
    METHOD_ALL = (1 << 64) - 1
    METHOD_FOREGROUND = METHOD_DXGI_DESKTOP_DUP_WINDOW | METHOD_SCREENDC
    METHOD_BACKGROUND = METHOD_FRAMEPOOL | METHOD_PRINTWINDOW

    def __init__(self, hwnd: Optional[int] = None, methods: int = METHOD_ALL):
        """
        hwnd: 平台相关目标（见类文档）。为 None 时按平台回退（Windows/Linux 语义见上）。
        methods: 参与测速的内置方式位掩码；只使用自定义单元时传 METHOD_NONE。
        """
        dll = _loader.load()
        self._setup_prototypes(dll)

        self._dll = dll
        self._handle = dll.MaaWSManagerCreate(hwnd or 0, methods)
        if not self._handle:
            raise RuntimeError("MaaWSManagerCreate failed")

        self._callbacks: list[ScreencapCallbacks] = []  # 持有回调对象引用，防 GC
        self._native_structs: list = []  # 持有回调结构体内存，C 侧保存了其指针

    @staticmethod
    def _setup_prototypes(dll: ctypes.CDLL) -> None:
        dll.MaaWSManagerCreate.restype = ctypes.c_void_p
        dll.MaaWSManagerCreate.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        dll.MaaWSManagerDestroy.restype = None
        dll.MaaWSManagerDestroy.argtypes = [ctypes.c_void_p]
        dll.MaaWSManagerSetPipeWireTarget.restype = MaaWSBool
        dll.MaaWSManagerSetPipeWireTarget.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_uint32]
        dll.MaaWSManagerSetWlrSocket.restype = MaaWSBool
        dll.MaaWSManagerSetWlrSocket.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        dll.MaaWSManagerSetMonitorIndex.restype = MaaWSBool
        dll.MaaWSManagerSetMonitorIndex.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        dll.MaaWSManagerRegisterCustomUnit.restype = MaaWSBool
        dll.MaaWSManagerRegisterCustomUnit.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p,
        ]
        dll.MaaWSManagerConnect.restype = MaaWSBool
        dll.MaaWSManagerConnect.argtypes = [ctypes.c_void_p]
        dll.MaaWSManagerScreencap.restype = MaaWSBool
        dll.MaaWSManagerScreencap.argtypes = [ctypes.c_void_p, ctypes.POINTER(ImageBuffer)]
        dll.MaaWSManagerSelectedUnitName.restype = ctypes.c_char_p
        dll.MaaWSManagerSelectedUnitName.argtypes = [ctypes.c_void_p]
        dll.MaaWSManagerInactive.restype = None
        dll.MaaWSManagerInactive.argtypes = [ctypes.c_void_p]

    # ---- Linux 截图目标配置（connect() 前调用；其他平台为 no-op）----

    def set_pipewire_target(self, fd: int = -1, node_id: int = 0) -> bool:
        """设置 PipeWire 目标：fd >= 0 时走 portal fd（连接后 fd 归库所有），
        否则连接会话 daemon。两者都未指定时 PipeWire 单元不参与构建。"""
        return bool(self._dll.MaaWSManagerSetPipeWireTarget(self._handle, fd, node_id))

    def set_wlr_socket(self, socket_name: Optional[str] = None) -> bool:
        """设置 wlr-screencopy 的 Wayland socket 名；None/空串 = WAYLAND_DISPLAY 默认值。"""
        return bool(
            self._dll.MaaWSManagerSetWlrSocket(
                self._handle, socket_name.encode("utf-8") if socket_name else None
            )
        )

    def set_monitor_index(self, index: int) -> bool:
        """设置整屏截图的目标显示器索引（connect() 前调用）。

        - Windows：METHOD_DXGI_DESKTOP_DUP 复制的显示器；0 = 主显示器，其余按
          EnumDisplayMonitors 顺序；设置后优先于 hwnd 定位，越界回退主显示器
        - Linux：METHOD_WLR 截取的 wl_output（合成器广播顺序，0 = 第一个）；
          PipeWire 的显示器选择发生在 portal 授权时，本设置不生效
        - macOS：hwnd 为 None 时 METHOD_SCREENCAPTUREKIT 截取的 display（0 = 第一个）
        """
        return bool(self._dll.MaaWSManagerSetMonitorIndex(self._handle, index))

    # ---- 通用接口 ----

    def register_custom_unit(self, name: str, callbacks: ScreencapCallbacks) -> bool:
        """注册外部自定义截图单元（C 回调实现）。"""
        # 保活：C 侧 CustomScreencapBridge 会保存 callbacks 结构体指针，
        # 并在 connect()/screencap() 时调用其中的函数指针；若 Python 侧被 GC，
        # 结构体内存与回调对象释放后即为悬垂指针（访问违例）。
        self._callbacks.append(callbacks)
        c_struct = callbacks._ctor()
        self._native_structs.append(c_struct)
        return bool(
            self._dll.MaaWSManagerRegisterCustomUnit(
                self._handle, name.encode("utf-8"), ctypes.cast(ctypes.byref(c_struct), ctypes.c_void_p), ctypes.c_void_p(id(callbacks))
            )
        )

    def connect(self) -> bool:
        """连接 + 测速选择最快截图方式。"""
        return bool(self._dll.MaaWSManagerConnect(self._handle))

    def screencap(self) -> Optional[ImageBuffer.Info]:
        """截图一次。成功返回 ImageBuffer.Info（width/height/channels/step/data），失败返回 None。"""
        buf = ImageBuffer()
        if not self._dll.MaaWSManagerScreencap(self._handle, ctypes.byref(buf)):
            return None
        return ImageBuffer.Info(
            width=buf.width,
            height=buf.height,
            channels=buf.channels,
            step=buf.step,
            data=buf.to_bytes(),
        )

    def selected_unit_name(self) -> Optional[str]:
        name = self._dll.MaaWSManagerSelectedUnitName(self._handle)
        return name.decode("utf-8") if name else None

    def inactive(self) -> None:
        self._dll.MaaWSManagerInactive(self._handle)

    def __del__(self):
        if getattr(self, "_handle", None):
            try:
                self._dll.MaaWSManagerDestroy(self._handle)
            except Exception:
                pass
            self._handle = None
