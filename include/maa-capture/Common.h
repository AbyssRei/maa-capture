#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// maa-capture - standalone Win32 screencap library
// Extracted from MaaFramework (https://github.com/MaaXYZ/MaaFramework)
//   source/MaaWin32ControlUnit/Screencap/*
// The extraction keeps the public behavior, but removes the dependency on
// MaaFramework/MaaUtils internals so the code can live and be built on its own.
// ============================================================================

// 命名空间与宏：原 MAA_CTRL_UNIT_NS_BEGIN/END 的对应物
#define MWS_NS_BEGIN namespace mws {
#define MWS_NS_END }

// 控制缩放的旧版兼容分支（与 MaaFramework 的 MAA_WIN32_COMPATIBLE 对应）：
// 默认使用 Per-Monitor V2 DPI 感知 + GetDpiForWindow；定义本宏则退化为
// 基于 GetMonitorInfo/EnumDisplaySettings 的缩放比计算。
#ifndef MWS_WIN32_COMPATIBLE
#define MWS_WIN32_COMPATIBLE 0
#endif

// FramePool (Windows.Graphics.Capture) 是否可用。
// 由 CMake 检测 Windows SDK + C++/WinRT 后定义（MWS_ENABLE_FRAMEPOOL）。
#ifndef MWS_FRAMEPOOL_AVAILABLE
#define MWS_FRAMEPOOL_AVAILABLE 0
#endif

// Linux PipeWire 截图单元是否可用（CMake 检测 libpipewire-0.3 与 libdrm 后定义）。
#ifndef MWS_PIPEWIRE_AVAILABLE
#define MWS_PIPEWIRE_AVAILABLE 0
#endif

// Linux wlr-screencopy（Wayland）截图单元是否可用（CMake 检测 wayland-client 后定义）。
#ifndef MWS_WLR_AVAILABLE
#define MWS_WLR_AVAILABLE 0
#endif

// macOS ScreenCaptureKit 截图单元是否可用（Apple 平台由 CMake 定义）。
#ifndef MWS_SCK_AVAILABLE
#define MWS_SCK_AVAILABLE 0
#endif

namespace mws
{
    using byte = std::uint8_t;
}

MWS_NS_BEGIN

// OnScopeLeave：作用域离开守卫（原 MaaUtils 的 OnScopeLeave 简化版）
namespace detail
{
    template <typename FuncT>
    class ScopeGuard
    {
    public:
        explicit ScopeGuard(FuncT&& func)
            : func_(std::forward<FuncT>(func))
        {
        }

        ~ScopeGuard()
        {
            if (active_) {
                func_();
            }
        }

        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

        ScopeGuard(ScopeGuard&& other) noexcept
            : func_(std::move(other.func_))
            , active_(other.active_)
        {
            other.active_ = false;
        }

        ScopeGuard& operator=(ScopeGuard&&) = delete;

    private:
        FuncT func_;
        bool active_ = true;
    };

    template <typename FuncT>
    ScopeGuard<FuncT> make_scope_guard(FuncT&& func)
    {
        return ScopeGuard<FuncT>(std::forward<FuncT>(func));
    }
}

#define MWS_CONCAT_IMPL(a, b) a##b
#define MWS_CONCAT(a, b) MWS_CONCAT_IMPL(a, b)

#define OnScopeLeave(...) auto MWS_CONCAT(mws_scope_guard_, __LINE__) = ::mws::detail::make_scope_guard(__VA_ARGS__)

// 距 start 的毫秒时长（原 MaaUtils/Time.hpp 的 duration_since）
template <typename ClockT = std::chrono::steady_clock, typename DurationT>
inline auto duration_since(const std::chrono::time_point<ClockT, DurationT>& start) noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(ClockT::now() - start);
}

MWS_NS_END