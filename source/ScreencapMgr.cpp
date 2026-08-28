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
    add(Method_DXGI_DesktopDup, "DXGI_DesktopDup", std::make_shared<DesktopDupScreencap>(hwnd));
    add(Method_DXGI_DesktopDup_Window, "DXGI_DesktopDup_Window", std::make_shared<DesktopDupWindowScreencap>(hwnd));
    add(Method_PrintWindow, "PrintWindow", std::make_shared<PrintWindowWithPseudoMinimizeScreencap>(hwnd));
    add(Method_ScreenDC, "ScreenDC", std::make_shared<ScreenDCScreencap>(hwnd));
#elif defined(__APPLE__)
    // hwnd_ 携带 CGWindowID（0 = 整个主显示器）
    const auto window_id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(hwnd_));
    add(Method_ScreenCaptureKit, "ScreenCaptureKit", std::make_shared<ScreenCaptureKitScreencap>(window_id));
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
            add(Method_All, "Wlr", std::make_shared<WlrScreencap>(std::move(client)));
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