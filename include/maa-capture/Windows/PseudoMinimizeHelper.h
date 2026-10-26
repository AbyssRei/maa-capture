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

#include <memory>

#include "maa-capture/Common.h"
#include "maa-capture/CaptureTypes.hpp"
#include "SafeWindows.hpp"

MWS_NS_BEGIN
namespace detail { struct WindowLeaseState; }

class PseudoMinimizeHelper
{
public:
    explicit PseudoMinimizeHelper(HWND hwnd);
    ~PseudoMinimizeHelper();

    PseudoMinimizeHelper(const PseudoMinimizeHelper&) = delete;
    PseudoMinimizeHelper& operator=(const PseudoMinimizeHelper&) = delete;

    bool start();
    bool stop();

    // 截图前的同步处理：最小化 -> 伪最小化
    bool ensure_not_minimized();
    bool validate_window();
    void require_no_activate() { strict_background_ = true; }

    bool is_pseudo_minimized() const;
    static bool logically_minimized(HWND hwnd);
    const MaaWSError& last_error() const { return error_; }

    // 恢复原始样式与不透明度（供包装类 inactive() 调用）
    bool revert_pseudo_minimize();

private:
    HWND hwnd_ = nullptr;
    std::shared_ptr<detail::WindowLeaseState> state_;
    uint64_t generation_ = 0;
    uint64_t lifetime_epoch_ = 0;
    bool running_ = false, leased_ = false;
    bool strict_background_ = false;
    MaaWSError error_ = capture_error(MAA_WS_OK, 0, "");
};

MWS_NS_END
