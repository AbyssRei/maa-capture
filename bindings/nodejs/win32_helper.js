'use strict';

// Windows 用户态辅助（FindWindow / GetForegroundWindow 等）

const koffi = require('koffi').default || require('koffi');

const user32 = koffi.load('user32.dll');

const fnEnumWindows = user32.func('BOOL EnumWindows(void *cb, LPARAM lparam)');
const fnGetWindowTextLengthW = user32.func('int32_t GetWindowTextLengthW(HWND hwnd)');
const fnGetWindowTextW = user32.func('int32_t GetWindowTextW(HWND hwnd, char16_t *buf, int32_t max)');
const fnGetForegroundWindow = user32.func('HWND GetForegroundWindow()');

function foregroundWindow() {
    const hwnd = fnGetForegroundWindow();
    return hwnd ? Number(hwnd) : null;
}

function findWindowByTitle(title) {
    let found = null;
    const cb = koffi.callback('BOOL cb(HWND hwnd, LPARAM lparam)', (hwnd) => {
        const len = fnGetWindowTextLengthW(hwnd);
        if (len > 0) {
            const buf = Buffer.alloc((len + 1) * 2);
            fnGetWindowTextW(hwnd, buf, len + 1);
            const text = buf.toString('utf16le').replace(/\u0000+$/, '');
            if (text.toLowerCase().includes(title.toLowerCase())) {
                found = Number(hwnd);
            }
        }
        return 1; // continue
    });
    fnEnumWindows(cb, 0);
    koffi.unregister(cb);
    return found;
}

module.exports = { foregroundWindow, findWindowByTitle };