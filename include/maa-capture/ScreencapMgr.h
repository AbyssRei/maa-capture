#pragma once

// ============================================================================
// 截图管理器 + 外部注册/测速选择器（跨平台）
//
// 提取自 MaaFramework source/MaaWin32ControlUnit/Manager/Win32ControlUnitMgr.cpp
// 的截图部分（build_screencap_units / speed_test / screencap）。
//
// 职责：
//   1. 按位掩码构建当前平台的内置截图单元（FramePool/PrintWindow 带伪最小化；
//      Linux 为 PipeWire / Wlr，macOS 为 ScreenCaptureKit）
//   2. 接受外部通过 ScreencapBase 继承接口注册的自定义单元
//   3. 预热 + 实测测速，自动选用最快可用方案
//
// hwnd 的平台语义：
//   Windows —— 目标窗口 HWND（可为 null，前台方式将不可用）
//   macOS   —— 目标窗口 CGWindowID（uintptr 转入；0 = 截取整个主显示器）
//   Linux   —— 忽略；PipeWire/Wlr 目标通过 set_pipewire_target /
//              set_wlr_socket 配置
// ============================================================================

#include "Common.h"
#include "ScreencapBase.h"

#if defined(_WIN32)
#include "Windows/SafeWindows.hpp"
#endif

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

MWS_NS_BEGIN

// ---- 内置截图方式位掩码 ----
// Win32 位 0-5 与 MaaFramework 的 Win32 截图方式枚举 取值一致；
// Linux（PipeWire/Wlr）与 macOS（ScreenCaptureKit）为新追加位。
using ScreencapMethod = std::uint64_t;
constexpr ScreencapMethod Method_None = 0ULL;
constexpr ScreencapMethod Method_GDI = 1ULL << 0;
constexpr ScreencapMethod Method_FramePool = 1ULL << 1;
constexpr ScreencapMethod Method_DXGI_DesktopDup = 1ULL << 2;
constexpr ScreencapMethod Method_DXGI_DesktopDup_Window = 1ULL << 3;
constexpr ScreencapMethod Method_PrintWindow = 1ULL << 4;
constexpr ScreencapMethod Method_ScreenDC = 1ULL << 5;
constexpr ScreencapMethod Method_PipeWire = 1ULL << 6; // Linux
constexpr ScreencapMethod Method_Wlr = 1ULL << 7; // Linux
constexpr ScreencapMethod Method_ScreenCaptureKit = 1ULL << 8; // macOS

constexpr ScreencapMethod Method_All = ~Method_None;
constexpr ScreencapMethod Method_Foreground = Method_DXGI_DesktopDup_Window | Method_ScreenDC;
constexpr ScreencapMethod Method_Background = Method_FramePool | Method_PrintWindow;

// 本构建在本平台可注册的内置截图方式位掩码（编译期视角；具体单元是否
// 实际可用由 connect() 的测速淘汰决定）。实现位于 .cpp，保证外部消费者
// 与库本体看到一致的结果（单元可用性宏只在库构建内可见）。
ScreencapMethod available_builtin_methods();

inline const char* method_name(ScreencapMethod method)
{
    switch (method) {
    case Method_GDI:
        return "GDI";
    case Method_FramePool:
        return "FramePool";
    case Method_DXGI_DesktopDup:
        return "DXGI_DesktopDup";
    case Method_DXGI_DesktopDup_Window:
        return "DXGI_DesktopDup_Window";
    case Method_PrintWindow:
        return "PrintWindow";
    case Method_ScreenDC:
        return "ScreenDC";
    case Method_PipeWire:
        return "PipeWire";
    case Method_Wlr:
        return "Wlr";
    case Method_ScreenCaptureKit:
        return "ScreenCaptureKit";
    default:
        return "Custom/Unknown";
    }
}

class ScreencapMgr
{
public:
    explicit ScreencapMgr(void* hwnd = nullptr);

    // ★ 外部继承接口入口：注册自定义截图单元（继承 ScreencapBase）。
    // 注册后与内置单元一起参与测速选择；成功返回 true。
    bool register_unit(std::string name, std::shared_ptr<ScreencapBase> unit);

    // 设置要参与测速的内置方式位掩码（默认 Method_All）。自定义单元不受此限制。
    // 未编译进当前平台的内置方式位会被静默忽略。
    void set_builtin_methods(ScreencapMethod methods);
    ScreencapMethod builtin_methods() const
    {
        return builtin_methods_;
    }

    // ---- Linux 截图目标配置（须在 connect() 前调用；其他平台为 no-op）----

    // PipeWire 目标。fd >= 0 时通过 portal fd 连接远端实例（连接后 fd 归
    // PipeWire 所有，调用方不得再使用）；否则连接会话 daemon。
    // 两者都未指定（fd<0 且 node_id=0）时 PipeWire 单元不参与构建。
    void set_pipewire_target(int fd, std::uint32_t node_id);

    // wlr-screencopy 使用的 Wayland socket 名；空串 = WAYLAND_DISPLAY 默认值。
    void set_wlr_socket(std::string socket_name);

    // ---- 整屏截图的显示器选择（须在 connect() 前调用）----
    // 各平台语义：
    //   Windows：Method_DXGI_DesktopDup 复制的显示器；0 = 主显示器，其余按
    //            EnumDisplayMonitors 顺序。设置后优先于 hwnd 定位。
    //   Linux  ：Method_Wlr 截取的 wl_output（合成器广播顺序，0 = 第一个）；
    //            PipeWire 的显示器选择发生在 portal 授权时，本设置不生效。
    //   macOS  ：hwnd 为空时 Method_ScreenCaptureKit 截取的 display
    //            （SCShareableContent.displays 顺序，0 = 第一个）。
    // 索引越界时对应单元在测速预热阶段失败并被淘汰（Windows 回退主显示器）。
    void set_monitor_index(std::uint32_t index);

    // ---- 显示器枚举（索引语义与 set_monitor_index 一致）----

    // 显示器数量。Linux 上需要一次 Wayland 连接（使用 set_wlr_socket 配置的
    // socket）；Wlr 单元未编入或连接失败时返回 0。macOS 需要 TCC 屏幕录制
    // 权限（SCShareableContent），无权限时返回 0。
    std::size_t monitor_count() const;

    struct MonitorInfo
    {
        bool primary = false;  // 是否主显示器（Windows：MonitorFromPoint(0,0)）
        std::string name;      // Windows: 设备名 "\\.\DISPLAY2"；macOS: 本地化名称；Linux: 空（协议不提供）
    };

    // 按索引取显示器信息；索引越界返回 nullopt。
    std::optional<MonitorInfo> monitor_info(std::size_t index) const;

    // 连接：构建内置单元（+外部已注册单元）→ 预热 → 测速 → 选定最快。
    // 返回 false 表示没有任何可用截图方式。
    bool connect();
    bool connected() const
    {
        return connected_;
    }

    // 截图（BGR 三通道，输出到 image）。未 connect 或所有方式失败时返回 false。
    bool screencap(Image& image);

    // 当前选中的截图单元名称（内置方式名为 "GDI"/"FramePool"/...，外部注册的为自定义名）
    const std::string& selected_unit_name() const
    {
        return selected_unit_name_;
    }

    // 释放资源 / 恢复窗口状态
    void inactive();

private:
    struct UnitEntry
    {
        std::string name;
        std::shared_ptr<ScreencapBase> unit;
    };

    void build_builtin_units();
    std::shared_ptr<ScreencapBase> speed_test();

    void* hwnd_ = nullptr;
    ScreencapMethod builtin_methods_ = Method_All;

    // Linux 截图目标（详见各 setter；非 Linux 平台不使用）
    int pipewire_fd_ = -1;
    std::uint32_t pipewire_node_id_ = 0;
    std::string wlr_socket_;

    // 整屏截图的显示器索引（-1 = 未指定，见 set_monitor_index）
    int monitor_index_ = -1;

    std::map<std::string, UnitEntry> units_;
    std::shared_ptr<ScreencapBase> selected_ = nullptr;
    std::string selected_unit_name_;

    bool connected_ = false;
};

MWS_NS_END
