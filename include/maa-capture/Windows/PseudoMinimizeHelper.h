#pragma once

// ============================================================================
// 伪最小化辅助（提取自 MaaFramework
// source/MaaWin32ControlUnit/Screencap/PseudoMinimizeHelper.*)
//
// 目标窗口被最小化时，WGC/PrintWindow 无法捕获（客户区不在屏幕上）。
// 伪最小化把窗口设为【透明 + 点击穿透】，再以不激活方式还原，
// 使截图在"看起来仍最小化"的前提下继续工作：
//   - 用户再次激活窗口后在下一次截图/停用时恢复原样式/透明度
//   - stop() 时恢复后再补一次 SW_MINIMIZE，保持用户的最小化意图
// ============================================================================

#include <atomic>
#include <mutex>

#include "maa-capture/Common.h"
#include "SafeWindows.hpp"

MWS_NS_BEGIN

class PseudoMinimizeHelper
{
public:
    explicit PseudoMinimizeHelper(HWND hwnd);
    ~PseudoMinimizeHelper();

    PseudoMinimizeHelper(const PseudoMinimizeHelper&) = delete;
    PseudoMinimizeHelper& operator=(const PseudoMinimizeHelper&) = delete;

    void start();
    void stop();

    // 截图前的同步处理：最小化 -> 伪最小化
    void ensure_not_minimized();

    bool is_pseudo_minimized() const
    {
        std::lock_guard lock(state_mutex_);
        return pseudo_minimized_;
    }

    // 恢复原始样式与不透明度（供包装类 inactive() 调用）
    void revert_pseudo_minimize();

private:
    void apply_pseudo_minimize();
    bool same_window() const;

    HWND hwnd_ = nullptr;

    std::atomic_bool running_ = false;
    mutable std::recursive_mutex state_mutex_;
    DWORD owner_pid_ = 0, owner_thread_ = 0;
    bool pseudo_minimized_ = false;

    LONG_PTR original_ex_style_ = 0;
    bool had_layered_style_ = false;
    BYTE original_alpha_ = 255;
    COLORREF original_color_key_ = 0;
    DWORD original_layered_flags_ = 0;
    DWORD pseudo_input_time_ = 0;
};

MWS_NS_END
