#pragma once

// ============================================================================
// 前台窗口工具（提取自 MaaFramework source/MaaWin32ControlUnit/Base/ForegroundUtils.h）
// ============================================================================

#include <chrono>
#include <thread>
#include <atomic>

#include "maa-capture/Common.h"
#include "SafeWindows.hpp"

MWS_NS_BEGIN

inline void ensure_foreground_and_topmost(HWND hwnd)
{
    if (!hwnd) {
        return;
    }

    // 如果窗口不在前台，先将其置顶
    if (hwnd != GetForegroundWindow()) {
        // 将窗口移到 Z 序顶部
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // 尝试设置为前台窗口
        SetForegroundWindow(hwnd);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // 再次检查，如果仍然不在前台，再次置顶
        if (hwnd != GetForegroundWindow()) {
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

inline bool ensure_foreground_with_cooldown(HWND hwnd)
{
    constexpr DWORD kForegroundRecoveryInterval = 5000;
    static std::atomic<DWORD> last_foreground_attempt {0};

    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }

    if (hwnd == GetForegroundWindow()) {
        return true;
    }

    DWORD now = GetTickCount();
    DWORD previous = last_foreground_attempt.load();
    for (;;) {
        if (previous && now - previous < kForegroundRecoveryInterval) return hwnd == GetForegroundWindow();
        if (last_foreground_attempt.compare_exchange_weak(previous,now)) break;
    }
    ensure_foreground_and_topmost(hwnd);
    return hwnd == GetForegroundWindow();
}

MWS_NS_END
