//! maa-capture —— Rust 绑定
//!
//! 对应 MaaFramework 的 maa-framework-rs 角色。封装截图管理器 C API：
//!   - `Manager`：内置 6 种 Win32 截图方式的测速选择 + 截图
//!   - `ScreencapCallbacks`：外部回调接口（对应 `MaaCustomControllerCallbacks::screencap`）
//!
//! 链接：`#[link(name = "maa-capture")]` 需要导入库 `maa-capture.lib`
//! （与 DLL 同目录）。构建时可用 `LIB` 环境变量指向该目录，运行时把 DLL 目录加入 PATH。
//!
//! ```no_run
//! use maa_capture::{Manager, Methods};
//! let hwnd = maa_capture::foreground_window();
//! if hwnd != 0 {
//!     let mgr = Manager::new(hwnd, Methods::ALL);
//!     if mgr.connect() {
//!         if let Some(img) = mgr.screencap() {
//!             println!("{}x{}", img.width, img.height);
//!         }
//!     }
//! }
//! ```

#![allow(clippy::missing_safety_doc)]

use std::ffi::{c_char, c_int, c_void};
use std::ptr;

// ---------------------------------------------------------------------------
// FFI 绑定（maa-capture.dll）
// ---------------------------------------------------------------------------

#[link(name = "maa-capture")]
unsafe extern "C" {
    fn MaaWSManagerCreate(hwnd: *mut c_void, methods: u64) -> *mut c_void;
    fn MaaWSManagerDestroy(handle: *mut c_void);
    fn MaaWSManagerRegisterCustomUnit(
        handle: *mut c_void,
        name: *const c_char,
        callbacks: *const ScreencapCallbacks,
        trans_arg: *mut c_void,
    ) -> u8;
    fn MaaWSManagerConnect(handle: *mut c_void) -> u8;
    fn MaaWSManagerScreencap(handle: *mut c_void, out: *mut ImageBuffer) -> u8;
    fn MaaWSManagerSelectedUnitName(handle: *mut c_void) -> *const c_char;
    fn MaaWSManagerInactive(handle: *mut c_void);
}

#[link(name = "user32")]
unsafe extern "system" {
    fn GetForegroundWindow() -> *mut c_void;
}

/// 当前前台窗口句柄（user32!GetForegroundWindow），无窗口返回 0。
pub fn foreground_window() -> usize {
    unsafe { GetForegroundWindow() as usize }
}

// ---------------------------------------------------------------------------
// 类型（与 MaaWSCustomScreencap.h 对齐）
// ---------------------------------------------------------------------------

/// 截图方式位掩码（与 C 头文件常数一致）
pub struct Methods;

impl Methods {
    pub const NONE: u64 = 0;
    pub const GDI: u64 = 1 << 0;
    pub const FRAMEPOOL: u64 = 1 << 1;
    pub const DXGI_DESKTOP_DUP: u64 = 1 << 2;
    pub const DXGI_DESKTOP_DUP_WINDOW: u64 = 1 << 3;
    pub const PRINTWINDOW: u64 = 1 << 4;
    pub const SCREENDC: u64 = 1 << 5;
    pub const ALL: u64 = u64::MAX;
    pub const FOREGROUND: u64 = Self::DXGI_DESKTOP_DUP_WINDOW | Self::SCREENDC;
    pub const BACKGROUND: u64 = Self::FRAMEPOOL | Self::PRINTWINDOW;
}

/// 输出图像缓冲（对应 C 的 MaaWSImageBuffer）。
/// `data` 指向 DLL 内部图像（BGR 三通道，行主序），有效期至下一次调用 / 销毁。
#[repr(C)]
pub struct ImageBuffer {
    pub data: *mut u8,
    pub width: c_int,
    pub height: c_int,
    pub channels: c_int,
    pub step: c_int,
}

impl ImageBuffer {
    /// 拷贝像素为 Vec<u8>（行主序，按 step 计算每行）。
    pub fn to_bytes(&self) -> Vec<u8> {
        if self.data.is_null() || self.width <= 0 || self.height <= 0 {
            return Vec::new();
        }
        let row = if self.step > 0 {
            self.step as usize
        } else {
            (self.width * self.channels) as usize
        };
        let total = row * self.height as usize;
        unsafe { std::slice::from_raw_parts(self.data, total).to_vec() }
    }
}

/// 外部回调类型（对应 MaaCustomControllerCallbacks 的回调）。
pub type BoolCallback = unsafe extern "C" fn(trans_arg: *mut c_void) -> u8;
pub type ScreencapCallback = unsafe extern "C" fn(trans_arg: *mut c_void, out: *mut ImageBuffer) -> u8;

/// 外部回调集合（对应 C 的 MaaWSCustomScreencapCallbacks）。
///
/// 字段可为 None（C 侧对应 NULL 函数指针）。
#[repr(C)]
pub struct ScreencapCallbacks {
    pub connect: Option<BoolCallback>,
    pub connected: Option<BoolCallback>,
    pub screencap: Option<ScreencapCallback>,
    pub inactive: Option<BoolCallback>,
}

// ---------------------------------------------------------------------------
// 截图管理器
// ---------------------------------------------------------------------------

/// 截图管理器（MaaWSManager*）。
pub struct Manager {
    handle: *mut c_void,
    /// 回调结构体由本对象持有（Box 固定地址），drop 时释放。
    callbacks: Vec<*mut ScreencapCallbacks>,
}

unsafe impl Send for Manager {}
unsafe impl Sync for Manager {}

impl Manager {
    /// 创建截图管理器。`hwnd` 为目标窗口句柄（0 = 无窗口，内置前台方式不可用）；
    /// `methods` 为内置方式位掩码。
    pub fn new(hwnd: usize, methods: u64) -> Self {
        let handle = unsafe { MaaWSManagerCreate(hwnd as *mut c_void, methods) };
        assert!(!handle.is_null(), "MaaWSManagerCreate failed");
        Manager { handle, callbacks: Vec::new() }
    }

    /// 注册外部自定义截图单元。
    ///
    /// `callbacks.screencap` 必须提供；`trans_arg` 原样透传给回调，
    /// 其生命周期由调用方管理（Manager 不负责释放）。
    pub fn register_custom_unit(&mut self, name: &str, callbacks: ScreencapCallbacks, trans_arg: *mut c_void) -> bool {
        assert!(callbacks.screencap.is_some(), "screencap callback is required");

        // Box 固定地址：C 侧保存该指针并在后续调用（connect/screencap 时）
        let boxed = Box::into_raw(Box::new(callbacks));
        let name_c = match std::ffi::CString::new(name) {
            Ok(s) => s,
            Err(_) => {
                unsafe { drop(Box::from_raw(boxed)) };
                return false;
            }
        };

        let ok = unsafe { MaaWSManagerRegisterCustomUnit(self.handle, name_c.as_ptr(), boxed, trans_arg) != 0 };
        if ok {
            self.callbacks.push(boxed);
            true
        } else {
            unsafe { drop(Box::from_raw(boxed)) };
            false
        }
    }

    /// 连接并测速选择最快截图方式。
    pub fn connect(&self) -> bool {
        unsafe { MaaWSManagerConnect(self.handle) != 0 }
    }

    /// 截图一次（BGR）。`data` 指向内部图像，需要持久数据请用 `to_bytes()`。
    pub fn screencap(&self) -> Option<ImageBuffer> {
        let mut out = ImageBuffer {
            data: ptr::null_mut(),
            width: 0,
            height: 0,
            channels: 0,
            step: 0,
        };
        if unsafe { MaaWSManagerScreencap(self.handle, &mut out) != 0 } {
            Some(out)
        } else {
            None
        }
    }

    /// 当前选中的截图单元名（内置如 "GDI"，自定义为注册名）。
    pub fn selected_unit_name(&self) -> Option<String> {
        let p = unsafe { MaaWSManagerSelectedUnitName(self.handle) };
        if p.is_null() {
            return None;
        }
        unsafe { Some(std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned()) }
    }

    /// 释放资源 / 恢复窗口状态（伪最小化等）。
    pub fn inactive(&self) {
        unsafe { MaaWSManagerInactive(self.handle) };
    }
}

impl Drop for Manager {
    fn drop(&mut self) {
        unsafe {
            MaaWSManagerDestroy(self.handle);
            for cb in &self.callbacks {
                drop(Box::from_raw(*cb));
            }
        }
        self.handle = ptr::null_mut();
        self.callbacks.clear();
    }
}