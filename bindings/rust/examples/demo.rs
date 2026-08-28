//! maa-capture Rust 绑定示例
//!
//! 构建：`LIB=D:\...\build\Release cargo build --example demo`
//! 运行：把 maa-capture.dll（及 OpenCV 运行库）所在目录加入 PATH，
//!       然后 `cargo run --example demo`

use std::ffi::c_void;

use maa_capture::{foreground_window, ImageBuffer, Manager, Methods, ScreencapCallbacks};

const W: i32 = 8;
const H: i32 = 6;

// const fn 生成 8x6 BGRA 渐变（静态数据，回调中直接引用，地址稳定）
const PIXELS: [u8; W as usize * H as usize * 4] = make_gradient();

const fn make_gradient() -> [u8; W as usize * H as usize * 4] {
    let mut px = [0u8; W as usize * H as usize * 4];
    let mut i = 0;
    let mut y = 0;
    while y < H {
        let mut x = 0;
        while x < W {
            px[i] = (x * 255 / W) as u8; // B
            px[i + 1] = (y * 255 / H) as u8; // G
            px[i + 2] = 128; // R
            px[i + 3] = 255; // A
            i += 4;
            x += 1;
        }
        y += 1;
    }
    px
}

// 外部截图回调：写入 BGRA 渐变（返回 1 = 成功）
unsafe extern "C" fn on_screencap(_trans_arg: *mut c_void, out: *mut ImageBuffer) -> u8 {
    if out.is_null() {
        return 0;
    }
    let img = unsafe { &mut *out };
    img.data = PIXELS.as_ptr() as *mut u8;
    img.width = W;
    img.height = H;
    img.channels = 4; // BGRA，桥接层转 BGR
    img.step = W * 4;
    1
}

unsafe extern "C" fn on_inactive(_trans_arg: *mut c_void) -> u8 {
    println!("[rust] callbacks.inactive invoked");
    1
}

fn test_custom_callback() {
    let mut mgr = Manager::new(0, Methods::NONE);
    let callbacks = ScreencapCallbacks {
        connect: None,
        connected: None,
        screencap: Some(on_screencap),
        inactive: Some(on_inactive),
    };
    if !mgr.register_custom_unit("rust-gradient", callbacks, std::ptr::null_mut()) {
        panic!("register_custom_unit failed");
    }
    if !mgr.connect() {
        panic!("connect failed");
    }
    println!("[rust] selected unit: {}", mgr.selected_unit_name().unwrap_or_default());

    let img = mgr.screencap().expect("screencap failed");
    let bytes = img.to_bytes();
    assert_eq!((img.width, img.height, img.channels), (W, H, 3), "unexpected image");
    assert_eq!(&bytes[..3], &[0, 0, 128], "unexpected first pixel");
    println!(
        "[rust] custom callback OK: {}x{} ch={} firstRGB={},{},{}",
        img.width, img.height, img.channels, bytes[0], bytes[1], bytes[2]
    );

    mgr.inactive();
}

fn test_builtin() {
    let hwnd = foreground_window();
    if hwnd == 0 {
        println!("[rust] SKIP builtin (no foreground window)");
        return;
    }
    let mgr = Manager::new(hwnd, Methods::ALL);
    if !mgr.connect() {
        println!("[rust] SKIP builtin (no method works on this window)");
        return;
    }
    let img = mgr.screencap().expect("builtin screencap failed");
    println!(
        "[rust] builtin OK: selected={} {}x{} ch={}",
        mgr.selected_unit_name().unwrap_or_default(),
        img.width,
        img.height,
        img.channels
    );
    mgr.inactive();
}

fn main() {
    test_custom_callback();
    test_builtin();
    println!("[rust] ALL PASS");
}