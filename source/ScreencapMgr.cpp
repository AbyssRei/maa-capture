#include "maa-capture/ScreencapMgr.h"

#include "maa-capture/Logger.h"

#if defined(_WIN32)
#include "maa-capture/DesktopDupScreencap.h"
#include "maa-capture/DesktopDupWindowScreencap.h"
#include "maa-capture/FramePoolScreencap.h"
#include "maa-capture/FramePoolWithPseudoMinimizeScreencap.h"
#include "maa-capture/GdiScreencap.h"
#include "maa-capture/PrintWindowScreencap.h"
#include "maa-capture/PrintWindowWithPseudoMinimizeScreencap.h"
#include "maa-capture/ScreenDCScreencap.h"
#include "Win32MonitorUtils.hpp"
#elif defined(__APPLE__)
#include "maa-capture/MacOS/ScreenCaptureKitScreencap.h"
#else
#if MWS_PIPEWIRE_AVAILABLE
#include "maa-capture/Linux/PipeWireScreencap.h"
#endif
#if MWS_WLR_AVAILABLE
#include "maa-capture/Linux/WaylandClient.h"
#include "maa-capture/Linux/WlrScreencap.h"
#endif
#endif

MWS_NS_BEGIN

ScreencapMethod available_builtin_methods()
{
#if defined(_WIN32)
    return Method_GDI | Method_FramePool | Method_DXGI_DesktopDup | Method_DXGI_DesktopDup_Window | Method_PrintWindow
           | Method_ScreenDC;
#elif defined(__APPLE__)
    return Method_ScreenCaptureKit;
#else
    ScreencapMethod methods = Method_None;
#if MWS_PIPEWIRE_AVAILABLE
    methods |= Method_PipeWire;
#endif
#if MWS_WLR_AVAILABLE
    methods |= Method_Wlr;
#endif
    return methods;
#endif
}

ScreencapMgr::ScreencapMgr(void* hwnd)
    : hwnd_(hwnd)
{
}

bool ScreencapMgr::register_unit(std::string name, std::shared_ptr<ScreencapBase> unit)
{
    if (name.empty() || !unit) {
        LogError << "Invalid unit" << VAR(name) << VAR_VOIDP(unit.get());
        return false;
    }

    auto [it, inserted] = units_.insert_or_assign(name, UnitEntry { name, std::move(unit) });
    if (!inserted) {
        LogWarn << "Unit already registered, replaced" << VAR(it->second.name);
    }
    return true;
}

void ScreencapMgr::set_builtin_methods(ScreencapMethod methods)
{
    builtin_methods_ = methods;
}

void ScreencapMgr::set_pipewire_target(int fd, std::uint32_t node_id)
{
    pipewire_fd_ = fd;
    pipewire_node_id_ = node_id;
}

void ScreencapMgr::set_wlr_socket(std::string socket_name)
{
    wlr_socket_ = std::move(socket_name);
}

void ScreencapMgr::set_monitor_index(std::uint32_t index)
{
    monitor_index_ = static_cast<int>(index);
}

std::size_t ScreencapMgr::monitor_count() const
{
#if defined(_WIN32)
    return win32::monitor_count();
#elif defined(__APPLE__)
    return sck_monitor_count();
#else
    #if MWS_WLR_AVAILABLE
    // 与 WlrScreencap 同源：临时连接 Wayland（使用 set_wlr_socket 的 socket），
    // registry roundtrip 后才知道 wl_output 列表
    WaylandClient client;
    if (!client.connect(wlr_socket_)) {
        LogWarn << "monitor_count: failed to connect Wayland, no output info" << VAR(wlr_socket_);
        return 0;
    }
    return client.output_count();
    #else
    LogWarn << "monitor_count: Wlr unit not built (wayland-client missing), no output info";
    return 0;
    #endif
#endif
}

std::optional<ScreencapMgr::MonitorInfo> ScreencapMgr::monitor_info(std::size_t index) const
{
    MonitorInfo info;
#if defined(_WIN32)
    if (!win32::monitor_info(index, info.primary, info.name)) {
        return std::nullopt;
    }
    return info;
#elif defined(__APPLE__)
    if (!sck_monitor_info(static_cast<std::uint32_t>(index), info.primary, info.name)) {
        return std::nullopt;
    }
    return info;
#else
    if (index >= monitor_count()) {
        return std::nullopt;
    }
    // wl_output 的名称/主输出信息需绑定 output listener（协议 version >= 4），
    // 暂不提供；索引本身即有效信息
    return info;
#endif
}

void ScreencapMgr::build_builtin_units()
{
    const auto add = [&](ScreencapMethod flag, const char* name, std::shared_ptr<ScreencapBase> unit) {
        if (builtin_methods_ & flag) {
            units_[name] = UnitEntry { name, std::move(unit) };
            LogDebug << "Builtin unit added" << VAR(name);
        }
    };

#if defined(_WIN32)
    const HWND hwnd = static_cast<HWND>(hwnd_);
    add(Method_GDI, "GDI", std::make_shared<GdiScreencap>(hwnd));
    add(Method_FramePool, "FramePool", std::make_shared<FramePoolWithPseudoMinimizeScreencap>(hwnd));
    // 整屏单元：显式显示器索引优先于 hwnd 定位（monitor_index_ < 0 时按原行为）
    add(Method_DXGI_DesktopDup, "DXGI_DesktopDup", std::make_shared<DesktopDupScreencap>(hwnd, monitor_index_));
    add(Method_DXGI_DesktopDup_Window, "DXGI_DesktopDup_Window", std::make_shared<DesktopDupWindowScreencap>(hwnd));
    add(Method_PrintWindow, "PrintWindow", std::make_shared<PrintWindowWithPseudoMinimizeScreencap>(hwnd));
    add(Method_ScreenDC, "ScreenDC", std::make_shared<ScreenDCScreencap>(hwnd));
#elif defined(__APPLE__)
    // hwnd_ 携带 CGWindowID（非 0 = 按窗口截取）；为 0 时整屏，可用显示器索引选择
    const auto window_id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(hwnd_));
    const auto display_index = monitor_index_ >= 0 ? static_cast<std::uint32_t>(monitor_index_) : 0;
    add(Method_ScreenCaptureKit, "ScreenCaptureKit", std::make_shared<ScreenCaptureKitScreencap>(window_id, display_index));
#else
    // Linux：单元按编译期可用性注册（依赖缺失时 CMake 已裁剪对应源文件）
#if MWS_PIPEWIRE_AVAILABLE
    if (builtin_methods_ & Method_PipeWire) {
        if (pipewire_fd_ < 0 && pipewire_node_id_ == 0) {
            // 无 portal fd 时必须显式指定节点, 否则会误连到任意节点
            LogWarn << "PipeWire target not set (fd < 0 and node id = 0), skipping PipeWire unit";
        }
        else {
            // 位掩码已在外层判断过，传 Method_All 保证 add 无条件注册
            add(Method_All, "PipeWire", std::make_shared<PipeWireScreencap>(pipewire_fd_, pipewire_node_id_));
        }
    }
#endif
#if MWS_WLR_AVAILABLE
    if (builtin_methods_ & Method_Wlr) {
        auto client = std::make_shared<WaylandClient>();
        if (client->connect(wlr_socket_)) {
            // 显示器索引：合成器广播 wl_output 的顺序（默认 0）
            const auto output_index = static_cast<std::size_t>(std::max(0, monitor_index_));
            add(Method_All, "Wlr", std::make_shared<WlrScreencap>(std::move(client), output_index));
        }
        else {
            LogWarn << "Failed to connect Wayland socket, skipping Wlr unit" << VAR(wlr_socket_);
        }
    }
#endif
#endif
}

bool ScreencapMgr::connect()
{
    connected_ = false;
    selected_.reset();
    selected_unit_name_.clear();

    if (builtin_methods_ != Method_None) {
        build_builtin_units();
    }

    if (units_.empty()) {
        LogError << "No screencap method to test (register a unit or enable builtin methods)";
        return false;
    }

    selected_ = speed_test();
    if (!selected_) {
        LogError << "failed to select screencap method";
        return false;
    }

    connected_ = true;
    return true;
}

std::shared_ptr<ScreencapBase> ScreencapMgr::speed_test()
{
    LogFunc;

    std::string fastest_name;
    std::shared_ptr<ScreencapBase> fastest;
    auto cost = std::chrono::milliseconds::max();

    auto check = [&](const std::string& name, std::chrono::steady_clock::time_point start) {
        auto duration = duration_since(start);
        if (duration < cost) {
            fastest_name = name;
            fastest = units_.at(name).unit;
            cost = duration;
        }
        LogInfo << VAR(name) << VAR(duration);
    };

    for (auto& [name, entry] : units_) {
        LogInfo << "Warming up" << VAR(name);
        if (!entry.unit->screencap()) {
            LogWarn << "failed to warm up" << VAR(name);
        }

        LogInfo << "Testing" << VAR(name);
        auto now = std::chrono::steady_clock::now();
        if (!entry.unit->screencap()) {
            LogWarn << "failed to test" << VAR(name);
            continue;
        }
        check(name, now);
    }

    if (!fastest) {
        LogError << "cannot find any method to screencap!";
        return nullptr;
    }

    LogInfo << "The fastest method is" << VAR(fastest_name) << VAR(cost);
    selected_unit_name_ = fastest_name;
    return fastest;
}

bool ScreencapMgr::screencap(Image& image)
{
    if (!connected_ || !selected_) {
        LogError << "not connected or no selected unit";
        return false;
    }

    auto opt = selected_->screencap();
    if (!opt) {
        LogError << "failed to screencap";
        return false;
    }

    image = std::move(opt).value();
    return true;
}

void ScreencapMgr::inactive()
{
    LogFunc;

    for (auto& [name, entry] : units_) {
        entry.unit->inactive();
    }
}

MWS_NS_END