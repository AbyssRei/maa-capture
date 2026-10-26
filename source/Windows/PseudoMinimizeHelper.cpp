#include "maa-capture/Windows/PseudoMinimizeHelper.h"

#include <chrono>

#include "maa-capture/Logger.h"

MWS_NS_BEGIN

PseudoMinimizeHelper::PseudoMinimizeHelper(HWND hwnd)
    : hwnd_(hwnd)
{
    owner_thread_ = GetWindowThreadProcessId(hwnd_, &owner_pid_);
}

PseudoMinimizeHelper::~PseudoMinimizeHelper()
{
    stop();
}

void PseudoMinimizeHelper::start()
{
    std::lock_guard lock(state_mutex_);
    if (!same_window()) return;
    if (running_) {
        return;
    }

    // 保存窗口原始扩展样式
    SetLastError(0);
    original_ex_style_ = GetWindowLongPtr(hwnd_, GWL_EXSTYLE);
    if (original_ex_style_ == 0 && GetLastError() != 0) {
        LogError << "GetWindowLongPtr failed" << VAR_VOIDP(hwnd_) << VAR(GetLastError());
        return;
    }
    had_layered_style_ = (original_ex_style_ & WS_EX_LAYERED) != 0;

    // 如果窗口原本就有 WS_EX_LAYERED，尝试读取原始透明度
    if (had_layered_style_) {
        if (!GetLayeredWindowAttributes(hwnd_, &original_color_key_, &original_alpha_, &original_layered_flags_)) {
            LogWarn << "Cannot read layered window state safely" << VAR_VOIDP(hwnd_) << VAR(GetLastError());
            return;
        }
    }
    else {
        original_alpha_ = 255;
    }

    running_ = true;

    LogInfo << "PseudoMinimizeHelper started" << VAR_VOIDP(hwnd_);
}

void PseudoMinimizeHelper::stop()
{
    if (!running_) {
        return;
    }

    running_ = false;

    std::lock_guard lock(state_mutex_);
    if (!same_window()) { pseudo_minimized_ = false; return; }

    // 确保退出时恢复窗口状态
    if (pseudo_minimized_) {
        revert_pseudo_minimize();
        // 用户期望窗口处于最小化状态，所以恢复属性后再最小化
        if (!ShowWindow(hwnd_, SW_MINIMIZE)) {
            LogWarn << "ShowWindow(SW_MINIMIZE) failed on stop" << VAR_VOIDP(hwnd_);
        }
    }

    LogInfo << "PseudoMinimizeHelper stopped" << VAR_VOIDP(hwnd_);
}

void PseudoMinimizeHelper::ensure_not_minimized()
{
    std::lock_guard lock(state_mutex_);
    if (!same_window()) {
        return;
    }
    LASTINPUTINFO input { sizeof(input) };
    if (pseudo_minimized_ && GetForegroundWindow() == hwnd_ && GetLastInputInfo(&input) && input.dwTime != pseudo_input_time_)
        revert_pseudo_minimize();

    if (IsIconic(hwnd_)) {
        apply_pseudo_minimize();
    }
}

void PseudoMinimizeHelper::apply_pseudo_minimize()
{
    std::lock_guard lock(state_mutex_);
    if (!running_ || !same_window()) return;
    LogInfo << "Applying pseudo-minimize" << VAR_VOIDP(hwnd_);

    // 1. 添加 WS_EX_LAYERED 和 WS_EX_TRANSPARENT 样式
    SetLastError(0);
    LONG_PTR ex_style = GetWindowLongPtr(hwnd_, GWL_EXSTYLE);
    if (ex_style == 0 && GetLastError() != 0) {
        LogError << "GetWindowLongPtr failed in apply" << VAR_VOIDP(hwnd_) << VAR(GetLastError());
        return;
    }
    SetLastError(0);
    if (SetWindowLongPtr(hwnd_, GWL_EXSTYLE, ex_style | WS_EX_LAYERED | WS_EX_TRANSPARENT) == 0 && GetLastError() != 0) {
        LogError << "SetWindowLongPtr failed in apply" << VAR_VOIDP(hwnd_) << VAR(GetLastError());
        return;
    }

    // 2. 设置完全透明
    if (!SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA)) {
        LogError << "SetLayeredWindowAttributes failed in apply" << VAR_VOIDP(hwnd_) << VAR(GetLastError());
        SetWindowLongPtr(hwnd_, GWL_EXSTYLE, original_ex_style_);
        return;
    }

    // 3. 以不激活方式还原窗口，使截图方式能工作
    LASTINPUTINFO input { sizeof(input) };
    if (GetLastInputInfo(&input)) pseudo_input_time_ = input.dwTime;
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);

    pseudo_minimized_ = true;
}

void PseudoMinimizeHelper::revert_pseudo_minimize()
{
    std::lock_guard lock(state_mutex_);
    if (!same_window()) { pseudo_minimized_ = false; return; }
    LogInfo << "Reverting pseudo-minimize" << VAR_VOIDP(hwnd_);

    // 1. 直接恢复到保存的原始扩展样式，避免位操作导致的样式丢失
    SetLastError(0);
    if (SetWindowLongPtr(hwnd_, GWL_EXSTYLE, original_ex_style_) == 0 && GetLastError() != 0) {
        LogError << "SetWindowLongPtr failed in revert" << VAR_VOIDP(hwnd_) << VAR(GetLastError());
    }

    // 2. 恢复透明度（需在恢复样式之后，因为原样式可能不含 WS_EX_LAYERED）
    if (had_layered_style_) {
        if (!SetLayeredWindowAttributes(hwnd_, original_color_key_, original_alpha_, original_layered_flags_)) {
            LogError << "SetLayeredWindowAttributes failed in revert" << VAR_VOIDP(hwnd_) << VAR(GetLastError());
        }
    }

    pseudo_minimized_ = false;
}

bool PseudoMinimizeHelper::same_window() const
{
    DWORD pid = 0;
    return IsWindow(hwnd_) && GetWindowThreadProcessId(hwnd_, &pid) == owner_thread_ && pid == owner_pid_;
}

MWS_NS_END
