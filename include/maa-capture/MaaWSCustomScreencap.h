#pragma once

// ============================================================================
// ★ 外部回调截图接口 —— 纯 C 头文件（C89/C99/C11 均可编译）
//
// 对应 MaaFramework 集成文档所述自定义控制器的截图回调契约
// （MaaCustomControllerCallbacks::screencap）：外部宿主实现回调，
// 回调把图像写入 out buffer。
//
// C++ 使用者可额外包含 CustomScreencap.h（含 CustomScreencapBridge，
// 可将回调包装为 ScreencapBase 单元注册进 ScreencapMgr 参与测速）。
// ============================================================================

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// ---- 导出宏 ----
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(MWS_BUILDING_LIBRARY)
#define MAA_WS_EXPORT __declspec(dllexport)
#elif defined(MWS_USE_LIBRARY)
#define MAA_WS_EXPORT __declspec(dllimport)
#else
#define MAA_WS_EXPORT
#endif
#else
#define MAA_WS_EXPORT __attribute__((visibility("default")))
#endif

    // ---- C 风格外部回调契约（语义对齐 MaaCustomControllerCallbacks 的截图部分）----

    typedef unsigned char MaaWSBool; // 0 / 1

    // 输出图像缓冲：回调把截图结果写入此结构。
    // 与原 MaaImageBuffer 的"out 参数写结果"语义一致。
    typedef struct MaaWSImageBuffer_
    {
        unsigned char* data; // 像素数据（BGRA 或 BGR）
        int32_t width;
        int32_t height;
        int32_t channels; // 4=BGRA（推荐）或 3=BGR
        int32_t step;     // 行字节数（stride）；<=0 表示连续存储
    } MaaWSImageBuffer;

    typedef struct MaaWSCustomScreencapCallbacks_
    {
        // 连接/查询。可以为 NULL。
        MaaWSBool (*connect)(void* trans_arg);
        MaaWSBool (*connected)(void* trans_arg);

        // 截图回调，必须实现：把图像写入 buffer，成功返回 1，失败返回 0。
        MaaWSBool (*screencap)(void* trans_arg, /*out*/ MaaWSImageBuffer* buffer);

        // 释放/恢复。可以为 NULL。
        MaaWSBool (*inactive)(void* trans_arg);
    } MaaWSCustomScreencapCallbacks;

    // ---- C API ----

    typedef void* MaaWSCustomScreencapHandle; // 不透明句柄（内部持有 CustomScreencapBridge）

    // 创建句柄。callbacks 不能为 NULL，且必须实现 screencap。
    MAA_WS_EXPORT MaaWSCustomScreencapHandle
        MaaWSCustomScreencapCreate(const MaaWSCustomScreencapCallbacks* callbacks, void* trans_arg);

    MAA_WS_EXPORT void MaaWSCustomScreencapDestroy(MaaWSCustomScreencapHandle handle);

    // 触发一次截图（走桥接的 screencap()），成功返回 1，输出写入 out_buffer。
    // 注意：输出被归一化为 BGR 三通道（与 MaaFramework 内部截图约定一致），
    // 即使回调写入的是 4 通道 BGRA，也会在桥接层完成 BGRA->BGR 转换。
    // out_buffer 指向的图像数据由句柄持有，有效期为下一次调用 / 销毁前。
    MAA_WS_EXPORT MaaWSBool MaaWSCustomScreencapScreencap(MaaWSCustomScreencapHandle handle, MaaWSImageBuffer* out_buffer);

    // ---------------------------------------------------------------------------
    // 截图管理器 C API（供各语言绑定使用的门面）
    //
    // 统一测速选择当前平台的内置截图方式并支持外部自定义截图单元注册，
    // 语义与 C++ 的 mws::ScreencapMgr 一致：
    //   Windows：GDI / FramePool / DXGI_DesktopDup / DXGI_DesktopDup_Window /
    //            PrintWindow / ScreenDC
    //   Linux  ：PipeWire（Method_PipeWire）/ Wlr（Method_Wlr）
    //   macOS  ：ScreenCaptureKit（Method_ScreenCaptureKit）
    // ---------------------------------------------------------------------------

    // 截图方式位掩码（与 C++ 的 mws::Method_* 取值一致）
    typedef uint64_t MaaWSMethod;

#define MAA_WS_METHOD_NONE 0ULL
#define MAA_WS_METHOD_GDI 1ULL
#define MAA_WS_METHOD_FRAMEPOOL (1ULL << 1)
#define MAA_WS_METHOD_DXGI_DESKTOP_DUP (1ULL << 2)
#define MAA_WS_METHOD_DXGI_DESKTOP_DUP_WINDOW (1ULL << 3)
#define MAA_WS_METHOD_PRINTWINDOW (1ULL << 4)
#define MAA_WS_METHOD_SCREENDC (1ULL << 5)
#define MAA_WS_METHOD_PIPEWIRE (1ULL << 6)
#define MAA_WS_METHOD_WLR (1ULL << 7)
#define MAA_WS_METHOD_SCREENCAPTUREKIT (1ULL << 8)
#define MAA_WS_METHOD_ALL (~0ULL)
#define MAA_WS_METHOD_FOREGROUND (MAA_WS_METHOD_DXGI_DESKTOP_DUP_WINDOW | MAA_WS_METHOD_SCREENDC)
#define MAA_WS_METHOD_BACKGROUND (MAA_WS_METHOD_FRAMEPOOL | MAA_WS_METHOD_PRINTWINDOW)

    typedef void* MaaWSManagerHandle; // 不透明句柄（内部持有 mws::ScreencapMgr + 图像缓存）

    // 创建管理器。hwnd 的平台语义：
    //   Windows —— 目标窗口 HWND（可为 NULL，此时内置前台方式将不可用）
    //   macOS   —— 目标窗口 CGWindowID 的整数值（0 = 截取整个主显示器）
    //   Linux   —— 忽略（PipeWire/Wlr 目标经下方 setter 配置）
    // methods 为参与测速的内置截图方式位掩码；只使用自定义单元时传 MAA_WS_METHOD_NONE。
    MAA_WS_EXPORT MaaWSManagerHandle MaaWSManagerCreate(void* hwnd, MaaWSMethod methods);

    MAA_WS_EXPORT void MaaWSManagerDestroy(MaaWSManagerHandle handle);

    // 设置 Linux PipeWire 目标（Connect 前调用；非 Linux 平台为 no-op，返回 1）。
    // fd >= 0 时通过 portal fd 连接远端实例（连接后 fd 归库所有，调用方不得再使用）；
    // fd < 0 时连接会话 PipeWire daemon。fd < 0 且 node_id == 0 时 PipeWire 单元不参与构建。
    MAA_WS_EXPORT MaaWSBool MaaWSManagerSetPipeWireTarget(MaaWSManagerHandle handle, int32_t fd, uint32_t node_id);

    // 设置 wlr-screencopy 使用的 Wayland socket 名（Connect 前调用；非 Linux 平台为 no-op，返回 1）。
    // socket_name 为 NULL 或空串时使用 WAYLAND_DISPLAY 环境变量默认值。
    MAA_WS_EXPORT MaaWSBool MaaWSManagerSetWlrSocket(MaaWSManagerHandle handle, const char* socket_name);

    // 设置整屏截图的目标显示器索引（Connect 前调用）。各平台语义：
    //   Windows：MAA_WS_METHOD_DXGI_DESKTOP_DUP 复制的显示器；0 = 主显示器，
    //            其余按 EnumDisplayMonitors 顺序；设置后优先于 hwnd 定位，
    //            越界回退主显示器。
    //   Linux  ：MAA_WS_METHOD_WLR 截取的 wl_output（合成器广播顺序，0 = 第一个）；
    //            PipeWire 的显示器选择发生在 portal 授权时，本设置不生效。
    //   macOS  ：hwnd 为 NULL/0 时 MAA_WS_METHOD_SCREENCAPTUREKIT 截取的
    //            display（displays 顺序，0 = 第一个）。
    MAA_WS_EXPORT MaaWSBool MaaWSManagerSetMonitorIndex(MaaWSManagerHandle handle, uint32_t index);

    // 注册外部自定义截图单元（用 C 回调实现，包装为 CustomScreencapBridge 参与测速）。
    // callbacks 的 screencap 必须实现。成功返回 1。
    MAA_WS_EXPORT MaaWSBool
        MaaWSManagerRegisterCustomUnit(MaaWSManagerHandle handle, const char* name, const MaaWSCustomScreencapCallbacks* callbacks, void* trans_arg);

    // 连接：构建内置单元 + 预热 + 实测测速，选定最快方式。成功返回 1。
    // 没有任何可用方式（内置全失败且无自定义单元）时返回 0。
    MAA_WS_EXPORT MaaWSBool MaaWSManagerConnect(MaaWSManagerHandle handle);

    // 触发一次截图，输出写入 out_buffer（BGR 三通道，指向句柄持有的数据，
    // 有效期至下一次调用 / 销毁）。未连接或截图失败返回 0。
    MAA_WS_EXPORT MaaWSBool MaaWSManagerScreencap(MaaWSManagerHandle handle, MaaWSImageBuffer* out_buffer);

    // 当前选中截图单元的名称（内置方式名如 "GDI"/"FramePool"，自定义为注册名）。
    // 返回的字符串由句柄持有，有效期至下一次调用 / 销毁；未连接返回 NULL。
    MAA_WS_EXPORT const char* MaaWSManagerSelectedUnitName(MaaWSManagerHandle handle);

    // 释放资源 / 恢复窗口状态（伪最小化等）。
    MAA_WS_EXPORT void MaaWSManagerInactive(MaaWSManagerHandle handle);

#ifdef __cplusplus
} // extern "C"
#endif