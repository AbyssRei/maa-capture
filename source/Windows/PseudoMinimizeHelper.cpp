#include "maa-capture/Windows/PseudoMinimizeHelper.h"
#include "maa-capture/Logger.h"
#include "WindowStateTransaction.hpp"
#include <map>
#include <mutex>

namespace mws::detail {
struct WindowIdentity {
    DWORD pid = 0, thread = 0;
    uint64_t created = 0;
    bool operator==(const WindowIdentity&) const = default;
};
static bool identity(HWND hwnd, WindowIdentity& out, MaaWSError& error) {
    out.thread = GetWindowThreadProcessId(hwnd, &out.pid);
    HANDLE process = out.thread ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, out.pid) : nullptr;
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool ok = process && GetProcessTimes(process, &created, &exited, &kernel, &user);
    const auto native = GetLastError();
    if (process) CloseHandle(process);
    if (!ok) {
        error = capture_error(!out.thread ? MAA_WS_TARGET_NOT_FOUND : native == ERROR_ACCESS_DENIED ? MAA_WS_PERMISSION_DENIED : MAA_WS_CAPTURE_FAILED,
            MAA_WS_STAGE_CAPTURE, "Cannot verify pseudo-minimize window identity", 0, native, "Win32"); return false;
    }
    out.created = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return true;
}
struct WindowLeaseState {
    std::recursive_mutex mutex;
    WindowIdentity owner;
    WindowStateTransaction transaction {WS_EX_LAYERED, WS_EX_TRANSPARENT, LWA_ALPHA};
    size_t holders = 0;
    size_t sessions = 0;
    uint64_t generation = 1;
    uint64_t lifetime_epoch = 1;
    DWORD input_time = 0;
    HWND foreground_before = nullptr;
    bool property_installed = false;
    bool nonactivating_restore = false;
};
// Registry locks only locate state. Never call Win32 or acquire a state lock
// while holding registry_mutex. Different windows have independent transitions.
static std::mutex registry_mutex;
static std::map<HWND, std::weak_ptr<WindowLeaseState>> registry;
static std::map<HWND, std::shared_ptr<WindowLeaseState>> recovery;
static std::shared_ptr<WindowLeaseState> lookup(HWND hwnd, bool create) {
    std::lock_guard lock(registry_mutex);
    for (auto it = registry.begin(); it != registry.end();) {
        if (it->second.expired()) it = registry.erase(it); else ++it;
    }
    auto found = registry.find(hwnd);
    if (found != registry.end()) if (auto state = found->second.lock()) return state;
    if (!create) return {};
    auto state = std::make_shared<WindowLeaseState>(); registry[hwnd] = state; return state;
}
static const wchar_t* property_name() {
    static const std::wstring name = L"maa-capture.lease." + std::to_wstring(reinterpret_cast<uintptr_t>(&registry));
    return name.c_str();
}
class NativeOps : public WindowStateOps {
public:
    NativeOps(HWND hwnd, WindowLeaseState& state, MaaWSError& error) : h(hwnd), s(state), e(error) {}
    bool valid() override {
        WindowIdentity current;
        if (!identity(h, current, e)) return false;
        if (!(current == s.owner) || (s.property_installed && GetPropW(h, property_name()) != &s)) {
            e = capture_error(MAA_WS_TARGET_CHANGED, MAA_WS_STAGE_CAPTURE, "Pseudo-minimize window lifetime changed"); return false;
        }
        return true;
    }
    bool read_style(uint64_t& out) override {
        SetLastError(0); out = GetWindowLongPtrW(h, GWL_EXSTYLE);
        return out || !GetLastError() ? true : failed("Cannot read window style");
    }
    bool write_style(uint64_t value) override {
        SetLastError(0);
        return SetWindowLongPtrW(h, GWL_EXSTYLE, LONG_PTR(value)) || !GetLastError() ? true : failed("Cannot write window style");
    }
    bool read_layered(LayeredAttributes& out) override {
        COLORREF color; DWORD flags; BYTE alpha;
        if (!GetLayeredWindowAttributes(h, &color, &alpha, &flags)) return failed("Cannot safely read layered attributes");
        // Win32 may retain the old color key when only LWA_ALPHA is set;
        // values outside flags have no semantic ownership.
        out = {flags & LWA_COLORKEY ? color : 0, flags, flags & LWA_ALPHA ? alpha : BYTE(255)}; return true;
    }
    bool write_layered(const LayeredAttributes& value) override {
        return SetLayeredWindowAttributes(h, value.color, value.alpha, value.flags) ? true : failed("Cannot write layered attributes");
    }
    bool show(bool minimize) override {
        if (!minimize) {
            LASTINPUTINFO input {sizeof(input)}; if (GetLastInputInfo(&input)) s.input_time = input.dwTime;
        }
        // Return value is previous visibility, not success.
        ShowWindow(h, minimize ? (s.nonactivating_restore ? SW_SHOWMINNOACTIVE : SW_MINIMIZE) : SW_SHOWNOACTIVATE);
        if (!minimize) s.foreground_before = GetForegroundWindow();
        if (!valid()) return false;
        if (bool(IsIconic(h)) != minimize) return failed("Window did not enter requested show state");
        return true;
    }
    bool user_restored() override {
        LASTINPUTINFO input {sizeof(input)};
        // Respect later external activation, including a programmatic restore
        // without new keyboard/mouse input. Snapshot after our own show call.
        return GetForegroundWindow() == h &&
            (s.foreground_before != h || (GetLastInputInfo(&input) && input.dwTime != s.input_time));
    }
    bool conflict() override {
        e = capture_error(MAA_WS_INVALID_STATE, MAA_WS_STAGE_CAPTURE, "External layered attributes changed; restoration ownership is ambiguous"); return false;
    }
private:
    bool failed(const char* message) {
        const auto code = GetLastError();
        e = capture_error(code == ERROR_ACCESS_DENIED ? MAA_WS_PERMISSION_DENIED : MAA_WS_CAPTURE_FAILED,
            MAA_WS_STAGE_CAPTURE, message, 0, code, "Win32"); return false;
    }
    HWND h; WindowLeaseState& s; MaaWSError& e;
};
static void retain(HWND h, const std::shared_ptr<WindowLeaseState>& s, bool failed) {
    std::lock_guard lock(registry_mutex);
    if (failed) recovery[h] = s; else recovery.erase(h);
}
}
namespace mws {
PseudoMinimizeHelper::PseudoMinimizeHelper(HWND hwnd) : hwnd_(hwnd) {}
PseudoMinimizeHelper::~PseudoMinimizeHelper() { try { stop(); } catch (...) {} }
bool PseudoMinimizeHelper::start() {
    if (running_) return true;
    if (!state_) state_ = detail::lookup(hwnd_, true);
    std::lock_guard lock(state_->mutex);
    detail::WindowIdentity current;
    if (!detail::identity(hwnd_, current, error_)) return false;
    detail::NativeOps ops(hwnd_, *state_, error_);
    if (state_->owner.thread && !ops.valid()) {
        state_->transaction.abandon(); state_->holders = state_->sessions = 0; state_->property_installed = false;
        ++state_->generation; ++state_->lifetime_epoch;
    }
    state_->owner = current;
    if (!state_->property_installed) {
        if (!SetPropW(hwnd_, detail::property_name(), state_.get())) {
            error_ = capture_error(MAA_WS_PERMISSION_DENIED, MAA_WS_STAGE_CAPTURE, "Cannot mark window lifetime", 0, GetLastError(), "Win32"); return false;
        }
        state_->property_installed = true;
    }
    lifetime_epoch_ = state_->lifetime_epoch;
    ++state_->sessions; running_ = true; return true;
}
bool PseudoMinimizeHelper::validate_window() {
    if (!running_ && !start()) return false;
    std::lock_guard lock(state_->mutex);
    if (lifetime_epoch_ != state_->lifetime_epoch) {
        error_ = capture_error(MAA_WS_TARGET_CHANGED, MAA_WS_STAGE_CAPTURE, "Window lifetime lease was replaced"); return false;
    }
    detail::NativeOps ops(hwnd_, *state_, error_); return ops.valid();
}
bool PseudoMinimizeHelper::ensure_not_minimized() {
    if (!validate_window()) return false;
    std::lock_guard lock(state_->mutex);
    detail::NativeOps ops(hwnd_, *state_, error_);
    if (!ops.valid()) { state_->transaction.abandon(); return false; }
    const bool had_mutation = state_->transaction.modified();
    if (!state_->transaction.observe(ops)) return false;
    if (had_mutation && !state_->transaction.modified()) { ++state_->generation; state_->holders = 0; }
    if (leased_ && generation_ != state_->generation) leased_ = false;
    if (!state_->transaction.modified() && IsIconic(hwnd_)) {
        state_->nonactivating_restore = strict_background_;
        if (!state_->property_installed) {
            if (!SetPropW(hwnd_, detail::property_name(), state_.get())) {
                error_ = capture_error(MAA_WS_PERMISSION_DENIED, MAA_WS_STAGE_CAPTURE, "Cannot mark window lease lifetime", 0, GetLastError(), "Win32"); return false;
            }
            state_->property_installed = true;
        }
        if (!state_->transaction.apply(ops)) {
            detail::retain(hwnd_, state_, state_->transaction.modified()); return false;
        }
        LogDebug << "pseudo-minimize applied generation=" << state_->generation;
    }
    if (state_->transaction.modified() && !leased_) {
        state_->nonactivating_restore |= strict_background_;
        ++state_->holders; leased_ = true; generation_ = state_->generation;
    }
    return true;
}
bool PseudoMinimizeHelper::stop() {
    if (!state_) return true;
    std::lock_guard lock(state_->mutex);
    if (lifetime_epoch_ && lifetime_epoch_ != state_->lifetime_epoch) {
        running_ = leased_ = false;
        error_ = capture_error(MAA_WS_TARGET_CHANGED, MAA_WS_STAGE_CAPTURE, "Window lifetime lease was replaced before release"); return false;
    }
    if (running_ && state_->sessions) --state_->sessions;
    running_ = false;
    detail::NativeOps ops(hwnd_, *state_, error_);
    bool ok = true;
    if (leased_ && generation_ == state_->generation && state_->holders) --state_->holders;
    leased_ = false;
    if (!state_->holders) {
        ok = state_->transaction.observe(ops) && state_->transaction.restore(ops, true);
        if (!state_->sessions && !state_->transaction.modified() && state_->property_installed) {
            if (ops.valid()) RemovePropW(hwnd_, detail::property_name());
            state_->property_installed = false;
        }
        detail::retain(hwnd_, state_, !ok && state_->transaction.modified());
    }
    if (!ok) LogError << "pseudo-minimize restore: " << error_.message << " native=" << error_.native_code;
    return ok;
}
bool PseudoMinimizeHelper::revert_pseudo_minimize() { return stop(); }
bool PseudoMinimizeHelper::is_pseudo_minimized() const {
    if (!state_) return false;
    std::lock_guard lock(state_->mutex);
    return leased_ && generation_ == state_->generation && state_->transaction.modified();
}
bool PseudoMinimizeHelper::logically_minimized(HWND hwnd) {
    auto state = detail::lookup(hwnd, false);
    if (!state) return IsIconic(hwnd);
    std::lock_guard lock(state->mutex);
    MaaWSError error {}; detail::NativeOps ops(hwnd, *state, error);
    if (!state->transaction.observe(ops)) return IsIconic(hwnd);
    return state->transaction.logical_minimize() || IsIconic(hwnd);
}
}
