// maa-capture —— NodeJS 绑定类型声明（对应 index.js）

/// <reference types="node" />

declare namespace maa-capture {
  /** MaaWSImageBuffer：像素 + 尺寸 + 步长 */
  interface ImageBuffer {
    data: Buffer | null;
    width: number;
    height: number;
    channels: number;
    step: number;
  }

  /** 截图方式位掩码（64 位 BigInt，与 C 头文件常数一致） */
  const METHODS: Readonly<{
    NONE: bigint;
    GDI: bigint;
    FRAMEPOOL: bigint;
    DXGI_DESKTOP_DUP: bigint;
    DXGI_DESKTOP_DUP_WINDOW: bigint;
    PRINTWINDOW: bigint;
    SCREENDC: bigint;
    ALL: bigint;
    FOREGROUND: bigint;
    BACKGROUND: bigint;
  }>;

  /** 外部截图回调（对应 MaaCustomControllerCallbacks::screencap）：把图像写入 out，成功返回 1 */
  type ScreencapCallback = (transArg: unknown, out: ImageBuffer) => number;

  /** 注册自定义单元的回调集合 */
  interface ScreencapCallbacks {
    screencap: ScreencapCallback;
    connect?: (transArg: unknown) => number;
    connected?: (transArg: unknown) => number;
    inactive?: (transArg: unknown) => number;
  }

  /** 截图结果 */
  interface ScreencapResult {
    width: number;
    height: number;
    channels: number; // 归一化为 3（BGR）
    step: number;
    data: Buffer; // 行主序像素
  }

  /** 截图管理器（内置 6 种 Win32 方式测速选择 + 外部单元注册） */
  class Manager {
    constructor(hwnd?: number | bigint | null, methods?: bigint);
    registerCustomUnit(name: string, callbacks: ScreencapCallbacks): boolean;
    connect(): boolean;
    screencap(): ScreencapResult | null;
    selectedUnitName(): string | null;
    inactive(): void;
    destroy(): void;
  }

  /** 设置 DLL 目录（可选） */
  function setDllPath(dir: string): void;
}

export = maa-capture;