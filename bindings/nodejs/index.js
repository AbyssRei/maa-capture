'use strict';

// ============================================================================
// maa-capture —— NodeJS 绑定（koffi FFI，无需 node-gyp）
//
// 依赖：npm i koffi
// 对应 MaaFramework 的 NodeJS 绑定角色；封装两个外部扩展接口：
//   1. 外部回调实现截图（对应 MaaCustomControllerCallbacks::screencap 契约）
//   2. Manager：内置 6 种 Win32 截图方式测速选择 + 外部单元注册
//
// 注意（koffi 3.x 语义）：
//   - 回调用 koffi.register 注册为"持久回调"（C 侧会在调用返回后继续使用），
//     使用完必须 koffi.unregister 释放槽位。
//   - 回调收到结构体指针参数时拿到的是 BigInt 指针地址，读写需 koffi.decode/encode。
//   - 回调结构体被 C 侧持有（CustomScreencapBridge 存指针），因此需要用
//     koffi.alloc + koffi.encode 生成持久化原生结构体，并保持存活。
//
// 用法：
//   const { Manager, METHODS } = require('./index.js');
//   const mgr = new Manager(null, METHODS.NONE);
//   mgr.registerCustomUnit('my-unit', {
//       screencap(ptr /* bigint */, arg) {
//           // 通过 koffi.encode(ImageBuffer, {...}, ptr) 写入像素
//           return 1;
//       },
//   });
//   mgr.connect();
//   const img = mgr.screencap(); // { width, height, channels, step, data: Buffer }
//   console.log(mgr.selectedUnitName());
// ============================================================================

const path = require('path');
const fs = require('fs');

const koffi = require('koffi').default || require('koffi');

// ---------------------------------------------------------------------------
// 动态库加载
// ---------------------------------------------------------------------------

function resolveDllPath() {
    const here = __dirname;
    const candidates = [
        process.env.MAA_WS_DLL_DIR && path.join(process.env.MAA_WS_DLL_DIR, 'maa-capture.dll'),
        path.join(here, '..', '..', 'build', 'Release', 'maa-capture.dll'),
        path.join(here, '..', '..', 'dist', 'bin', 'maa-capture.dll'),
        'maa-capture.dll',
    ].filter(Boolean);

    for (const p of candidates) {
        if (fs.existsSync(p)) return p;
    }
    return 'maa-capture.dll'; // 交给系统 PATH
}

let _dll = null;
function dll() {
    if (_dll) return _dll;
    const resolved = resolveDllPath();
    try {
        _dll = koffi.load(resolved);
    }
    catch (e) {
        throw new Error(`无法加载 ${resolved}: ${e.message}`);
    }
    return _dll;
}

// ---------------------------------------------------------------------------
// 类型（与 MaaWSCustomScreencap.h 对齐）
// ---------------------------------------------------------------------------

// MaaWSBool = unsigned char（0/1）；注册为 koffi 可识别的类型别名
koffi.alias('MaaWSBool', 'uint8');

const ImageBuffer = koffi.struct('MaaWSImageBuffer', {
    data: koffi.pointer('uint8'),
    width: 'int32',
    height: 'int32',
    channels: 'int32',
    step: 'int32',
});

// 回调原型（cdecl；函数指针字段）
const ConnectCb = koffi.proto('MaaWSBool mws_connect(void *)');
const ConnectedCb = koffi.proto('MaaWSBool mws_connected(void *)');
const ScreencapCb = koffi.proto('MaaWSBool mws_screencap(void *, MaaWSImageBuffer *)');
const InactiveCb = koffi.proto('MaaWSBool mws_inactive(void *)');

const CustomScreencapCallbacks = koffi.struct('MaaWSCustomScreencapCallbacks', {
    connect: koffi.pointer(ConnectCb),
    connected: koffi.pointer(ConnectedCb),
    screencap: koffi.pointer(ScreencapCb),
    inactive: koffi.pointer(InactiveCb),
});

// 截图方式位掩码（与 C 头文件常数一致）
const METHODS = Object.freeze({
    NONE: 0n,
    GDI: 1n << 0n,
    FRAMEPOOL: 1n << 1n,
    DXGI_DESKTOP_DUP: 1n << 2n,
    DXGI_DESKTOP_DUP_WINDOW: 1n << 3n,
    PRINTWINDOW: 1n << 4n,
    SCREENDC: 1n << 5n,
    ALL: (1n << 64n) - 1n,
    FOREGROUND: (1n << 3n) | (1n << 5n),
    BACKGROUND: (1n << 1n) | (1n << 4n),
});

// ---------------------------------------------------------------------------
// 截图管理器（MaaWSManager*）
// ---------------------------------------------------------------------------

class Manager {
    /**
     * @param {number|bigint|null} hwnd 目标窗口句柄；null 时内置前台方式不可用
     * @param {bigint} methods 参与测速的内置方式位掩码（默认 ALL）
     */
    constructor(hwnd = null, methods = METHODS.ALL) {
        const lib = dll();
        this._lib = lib;

        this._fnCreate = lib.func('void * MaaWSManagerCreate(void *hwnd, uint64_t methods)');
        this._fnDestroy = lib.func('void MaaWSManagerDestroy(void *handle)');
        this._fnReg = lib.func('MaaWSBool MaaWSManagerRegisterCustomUnit(void *handle, const char *name, const MaaWSCustomScreencapCallbacks *callbacks, void *trans_arg)');
        this._fnConnect = lib.func('MaaWSBool MaaWSManagerConnect(void *handle)');
        this._fnScreencap = lib.func('MaaWSBool MaaWSManagerScreencap(void *handle, MaaWSImageBuffer *out_buffer)');
        this._fnSelected = lib.func('const char * MaaWSManagerSelectedUnitName(void *handle)');
        this._fnInactive = lib.func('void MaaWSManagerInactive(void *handle)');

        this._handle = this._fnCreate(hwnd ?? null, methods);
        if (!this._handle) throw new Error('MaaWSManagerCreate failed');

        // 持有回调注册句柄与原生结构体（防泄漏/GC）
        this._registered = []; // { handle, unregister }
        this._nativeStructs = []; // Buffer（持久化结构体内存）
    }

    /**
     * 注册外部自定义截图单元。
     * @param {string} name
     * @param {{screencap: Function, connect?: Function, connected?: Function, inactive?: Function}} fns
     * @returns {boolean}
     */
    registerCustomUnit(name, fns) {
        if (!fns || typeof fns.screencap !== 'function') {
            throw new TypeError('screencap callback is required');
        }

        const reg = (fn, protoType) => {
            if (!fn) return 0n;
            const h = koffi.register(fn, koffi.pointer(protoType));
            this._registered.push(h);
            return h;
        };

        const structObj = {
            connect: reg(fns.connect, ConnectCb),
            connected: reg(fns.connected, ConnectedCb),
            screencap: reg(fns.screencap, ScreencapCb),
            inactive: reg(fns.inactive, InactiveCb),
        };

        // 持久化原生结构体：C 侧 CustomScreencapBridge 会保存该指针并在后续调用
        const buf = koffi.alloc(CustomScreencapCallbacks, 1);
        koffi.encode(buf, CustomScreencapCallbacks, structObj);
        this._nativeStructs.push(buf);

        return !!this._fnReg(this._handle, name, buf, null);
    }

    /** 连接 + 测速选择最快截图方式 */
    connect() {
        return !!this._fnConnect(this._handle);
    }

    /**
     * 截图一次。
     * @returns {{width:number, height:number, channels:number, step:number, data:Buffer}|null}
     */
    screencap() {
        const out = koffi.alloc(ImageBuffer, 1);
        this._nativeStructs.push(out); // 保持存活
        const ok = this._fnScreencap(this._handle, out);
        if (!ok) return null;

        const info = koffi.decode(out, ImageBuffer);
        if (info.width <= 0 || info.height <= 0 || info.data === 0n) return null;

        const row = info.step > 0 ? info.step : info.width * info.channels;
        // 行主序拷贝
        const data = Buffer.alloc(row * info.height);
        for (let y = 0; y < info.height; y++) {
            const src = koffi.decode(info.data + BigInt(y) * BigInt(info.step), 'uint8', info.width * info.channels);
            data.set(Buffer.from(src), y * row);
        }

        return {
            width: info.width,
            height: info.height,
            channels: info.channels,
            step: info.step,
            data,
        };
    }

    /** 选中的截图单元名（内置如 "GDI"，自定义为注册名） */
    selectedUnitName() {
        // koffi 自动把 const char * 解码为 JS 字符串；返回空指针时为 null
        return this._fnSelected(this._handle) || null;
    }

    inactive() {
        this._fnInactive(this._handle);
    }

    destroy() {
        if (!this._handle) return;
        this._fnDestroy(this._handle);
        this._handle = null;

        // 释放原生结构体内存
        for (const buf of this._nativeStructs) {
            try { koffi.free(buf); } catch { /* noop */ }
        }
        this._nativeStructs = [];

        // 释放持久回调槽位
        for (const h of this._registered) {
            try { koffi.unregister(h); } catch { /* noop */ }
        }
        this._registered = [];
    }
}

// ---------------------------------------------------------------------------
// C 风格回调句柄（MaaWSCustomScreencap*）
// ---------------------------------------------------------------------------

class CustomScreencap {
    /**
     * @param {{screencap: Function, connect?: Function, connected?: Function, inactive?: Function}} fns
     */
    constructor(fns) {
        const lib = dll();
        this._lib = lib;

        this._fnCreate = lib.func('void * MaaWSCustomScreencapCreate(const MaaWSCustomScreencapCallbacks *callbacks, void *trans_arg)');
        this._fnDestroy = lib.func('void MaaWSCustomScreencapDestroy(void *handle)');
        this._fnScreencap = lib.func('MaaWSBool MaaWSCustomScreencapScreencap(void *handle, MaaWSImageBuffer *out_buffer)');

        const reg = (fn, protoType) => (fn ? koffi.register(fn, koffi.pointer(protoType)) : 0n);

        const structObj = {
            connect: reg(fns.connect, ConnectCb),
            connected: reg(fns.connected, ConnectedCb),
            screencap: reg(fns.screencap, ScreencapCb),
            inactive: reg(fns.inactive, InactiveCb),
        };

        const buf = koffi.alloc(CustomScreencapCallbacks, 1);
        koffi.encode(buf, CustomScreencapCallbacks, structObj);

        this._handle = this._fnCreate(buf, null);
        if (!this._handle) throw new Error('MaaWSCustomScreencapCreate failed');

        this._nativeStruct = buf;
        this._registered = Object.values(structObj).filter((v) => v !== 0n);
    }

    screencap() {
        const out = koffi.alloc(ImageBuffer, 1);
        const ok = this._fnScreencap(this._handle, out);
        if (!ok) { koffi.free(out); return null; }
        const info = koffi.decode(out, ImageBuffer);
        koffi.free(out);
        return info;
    }

    destroy() {
        if (!this._handle) return;
        this._fnDestroy(this._handle);
        this._handle = null;
        try { koffi.free(this._nativeStruct); } catch { /* noop */ }
        for (const h of this._registered) {
            try { koffi.unregister(h); } catch { /* noop */ }
        }
        this._registered = [];
    }
}

// ---------------------------------------------------------------------------
// Windows 辅助（可选）
// ---------------------------------------------------------------------------

koffi.alias('BOOL', 'int32');
koffi.alias('LPARAM', 'intptr_t');

const _user32 = koffi.load('user32.dll');
const _fnEnumWindows = _user32.func('BOOL EnumWindows(void *cb, LPARAM lparam)');
const _fnGetWindowTextLengthW = _user32.func('int32_t GetWindowTextLengthW(void *hwnd)');
const _fnGetWindowTextW = _user32.func('int32_t GetWindowTextW(void *hwnd, char16_t *buf, int32_t max)');
const _fnGetForegroundWindow = _user32.func('void * GetForegroundWindow()');

function foregroundWindow() {
    const hwnd = _fnGetForegroundWindow();
    return hwnd ? Number(hwnd) : null;
}

function findWindowByTitle(title) {
    let found = null;
    _fnEnumWindows((hwnd) => {
        const len = _fnGetWindowTextLengthW(hwnd);
        if (len > 0) {
            const buf = Buffer.alloc((len + 1) * 2);
            _fnGetWindowTextW(hwnd, buf, len + 1);
            const text = buf.toString('utf16le').replace(/\u0000+$/, '');
            if (text.toLowerCase().includes(title.toLowerCase())) {
                found = Number(hwnd);
                return 0; // 停止枚举
            }
        }
        return 1; // 继续
    }, 0);
    return found;
}

module.exports = {
    Manager,
    CustomScreencap,
    ImageBuffer,
    METHODS,
    dll,
    setDllPath(dir) {
        _dll = null;
        if (dir) process.env.MAA_WS_DLL_DIR = dir;
    },
    foregroundWindow,
    findWindowByTitle,
};