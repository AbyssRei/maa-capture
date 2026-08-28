#pragma once

// ============================================================================
// 伪最小化辅助（提取自 MaaFramework
// source/MaaWin32ControlUnit/Screencap/PseudoMinimizeHelper.*)
//
// 目标窗口被最小化时，WGC/PrintWindow 无法捕获（客户区不在屏幕上）。
// 伪最小化把窗口设为【透明 + 点击穿透】，再以不激活方式还原，
// 使截图在"看起来仍最小化"的前提下继续工作：
//   - 用户再次激活窗口时自动恢复原样式/透明度（后台轮询）
//   - stop() 时恢复后再补一次 SW_MINIMIZE，保持用户的最小化意图
// ============================================================================

#include <thread>

#include "Common.h"
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
        return pseudo_minimized_;
    }

    // 恢复原始样式与不透明度（供包装类 inactive() 调用）
    void revert_pseudo_minimize();

private:
    // 伪最小化 -> 恢复（用户重新激活窗口时）
    void monitor_thread_func();
    void apply_pseudo_minimize();

    HWND hwnd_ = nullptr;

    bool running_ = false;
    bool pseudo_minimized_ = false;
    std::thread monitor_thread_;

    LONG_PTR original_ex_style_ = 0;
    bool had_layered_style_ = false;
    BYTE original_alpha_ = 255;
};

MWS_NS_END