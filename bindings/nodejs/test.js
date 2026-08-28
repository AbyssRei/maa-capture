'use strict';

// maa-capture NodeJS 绑定自测
// 运行：npm i koffi && node test.js [dll目录]

const { Manager, METHODS, ImageBuffer, setDllPath, findWindowByTitle, foregroundWindow } = require('./index.js');
const koffi = require('koffi').default || require('koffi');

if (process.argv[2]) {
    setDllPath(process.argv[2]);
}

const W = 8;
const H = 6;

// ---- 生成 BGRA 渐变（外部图像源） ----
function makeGradient() {
    const px = Buffer.alloc(W * H * 4);
    let i = 0;
    for (let y = 0; y < H; y++) {
        for (let x = 0; x < W; x++) {
            px[i++] = Math.floor((x * 255) / W); // B
            px[i++] = Math.floor((y * 255) / H); // G
            px[i++] = 128; // R
            px[i++] = 255; // A
        }
    }
    return px;
}

const pixels = makeGradient();
let s_storage = Buffer.from(pixels); // 持久引用，防 GC

// 外部截图回调：签名 (trans_arg, MaaWSImageBuffer*)，指针以 BigInt 传入
function onScreencap(_transArg, imgPtr) {
    koffi.encode(imgPtr, ImageBuffer, {
        data: koffi.as(s_storage, 'uint8 *'),
        width: W,
        height: H,
        channels: 4,
        step: W * 4,
    });
    return 1;
}

function onInactive() {
    console.log('[node] callbacks.inactive invoked');
    return 1;
}

function main() {
    // 1) 外部回调单元注册进 Manager（不启用内置方式，无窗口依赖）
    const mgr = new Manager(null, METHODS.NONE);
    if (!mgr.registerCustomUnit('node-gradient', { screencap: onScreencap, inactive: onInactive })) {
        throw new Error('registerCustomUnit failed');
    }
    if (!mgr.connect()) {
        throw new Error('connect failed');
    }
    const name = mgr.selectedUnitName();
    if (name !== 'node-gradient') {
        throw new Error(`unexpected selected unit: ${name}`);
    }
    console.log(`[node] selected unit: ${name}`);

    const img = mgr.screencap();
    if (!img) {
        throw new Error('screencap failed');
    }
    if (img.width !== W || img.height !== H || img.channels !== 3) {
        throw new Error(`unexpected image: ${img.width}x${img.height} ch=${img.channels}`);
    }
    if (img.data[0] !== 0 || img.data[1] !== 0 || img.data[2] !== 128) {
        throw new Error(`unexpected first pixel: ${img.data.slice(0, 3)}`);
    }
    console.log(`[node] custom callback unit OK: ${img.width}x${img.height} ch=${img.channels}, first pixel RGB=${img.data.slice(0, 3)}`);

    mgr.inactive();
    mgr.destroy();

    // 2) 内置方式（需要真实窗口；无窗口/全失败时跳过）
    const hwnd = foregroundWindow();
    if (!hwnd) {
        console.log('[node] SKIP builtin (no foreground window)');
        console.log('[node] ALL PASS');
        return;
    }
    const m2 = new Manager(hwnd, METHODS.ALL);
    if (!m2.connect()) {
        console.log('[node] SKIP builtin (no method works on this window)');
        console.log('[node] ALL PASS');
        return;
    }
    const img2 = m2.screencap();
    if (!img2) {
        throw new Error('builtin screencap failed');
    }
    console.log(`[node] builtin manager OK: selected=${m2.selectedUnitName()} ${img2.width}x${img2.height} ch=${img2.channels}`);
    m2.destroy();
    console.log('[node] ALL PASS');
}

main();