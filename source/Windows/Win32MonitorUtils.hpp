#pragma once

// ============================================================================
// Windows 显示器枚举（库内部共享）
//
// 供 DesktopDupScreencap（按索引定位输出）与 ScreencapMgr 的显示器查询
// API 共用，保证两处索引语义一致：主显示器固定排索引 0，其余按
// EnumDisplayMonitors 顺序。
// ============================================================================

#if !defined(_WIN32)
#error "Win32MonitorUtils is Windows-only"
#endif

#include "maa-capture/Common.h"
#include "maa-capture/Windows/SafeWindows.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

MWS_NS_BEGIN

namespace win32
{
    // 枚举全部显示器；主显示器（MonitorFromPoint(0,0)）固定排到最前
    inline std::vector<HMONITOR> monitors_primary_first()
    {
        struct EnumContext
        {
            std::vector<HMONITOR> monitors;
        } ctx;

        EnumDisplayMonitors(
            nullptr,
            nullptr,
            [](HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lparam) -> BOOL {
                std::ignore = hdc;
                std::ignore = rect;
                reinterpret_cast<EnumContext*>(lparam)->monitors.push_back(hmon);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&ctx));

        const HMONITOR primary = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        if (primary) {
            auto it = std::find(ctx.monitors.begin(), ctx.monitors.end(), primary);
            if (it != ctx.monitors.end() && it != ctx.monitors.begin()) {
                std::iter_swap(it, ctx.monitors.begin());
            }
        }
        return ctx.monitors;
    }

    inline std::size_t monitor_count()
    {
        return monitors_primary_first().size();
    }

    // 索引越界返回 false。name 为设备名（MONITORINFOEX.szDevice，如 "\\.\DISPLAY2"）
    inline bool monitor_info(std::size_t index, bool& primary, std::string& name)
    {
        const auto monitors = monitors_primary_first();
        if (index >= monitors.size()) {
            return false;
        }

        MONITORINFOEXW mi { };
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(monitors[index], &mi)) {
            return false;
        }

        char device_name[CCHDEVICENAME + 1] = { 0 };
        const int written = WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, device_name, sizeof(device_name), nullptr, nullptr);
        primary = monitors[index] == MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        name = written > 0 ? device_name : "";
        return true;
    }
}

MWS_NS_END
