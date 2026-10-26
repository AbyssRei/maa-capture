#include "CaptureInternal.hpp"
#include <chrono>
#include <cstring>
#if defined(_WIN32)
#include "maa-capture/Windows/SafeWindows.hpp"
#endif
namespace mws::detail {
#if !defined(__APPLE__)
bool resolve_source(const CaptureTarget& t, MaaWSFrameInfo& i, uint64_t& identity, MaaWSError& e) {
    i = {}; i.struct_size = sizeof(i); i.target_type = t.type; i.scope = target_scope(t);
    i.axis = MAA_WS_AXIS_X_RIGHT_Y_DOWN; i.limitations = MAA_WS_LIMIT_NONATOMIC;
    if (t.type == MAA_WS_TARGET_PIPEWIRE || t.type == MAA_WS_TARGET_WLR) {
        i.coordinate_unit = MAA_WS_UNIT_PROVIDER; i.coordinate_space = MAA_WS_SPACE_SOURCE;
        i.pixel_semantics = MAA_WS_PIXELS_PROVIDER; i.verification = MAA_WS_VERIFY_PROVIDER;
        i.limitations |= MAA_WS_LIMIT_PROVIDER_IDENTITY;
        copy_text(i.source_key, t.type == MAA_WS_TARGET_WLR ? "wlr:session:" + std::string(t.socket_name) + ":" + std::to_string(t.output_index) : "pipewire:node:" + std::to_string(t.node_id));
        return true;
    }
#if defined(_WIN32)
    auto snapshot = MonitorSnapshot::create(e); if (!snapshot) return false;
    i.layout_before = snapshot->layout_token;
    i.coordinate_unit = MAA_WS_UNIT_PHYSICAL_PIXEL; i.coordinate_space = MAA_WS_SPACE_DESKTOP;
    i.pixel_semantics = MAA_WS_PIXELS_NATIVE; i.verification = MAA_WS_VERIFY_SAMPLED;
    if (t.type == MAA_WS_TARGET_MONITOR) {
        const MaaWSMonitorInfo* found = nullptr;
        for (const auto& m : snapshot->items) if (std::strcmp(m.monitor_key, t.source_key) == 0) {
            if (found || m.identity_scope == MAA_WS_IDENTITY_AMBIGUOUS) { e = capture_error(MAA_WS_TARGET_AMBIGUOUS, MAA_WS_STAGE_CAPTURE, "Monitor key does not identify a unique output"); return false; }
            found = &m;
        }
        if (!found) { e = capture_error(MAA_WS_TARGET_NOT_FOUND, MAA_WS_STAGE_CAPTURE, "Monitor key is absent"); return false; }
        i.native_id = found->native_id; i.desktop_rect = found->desktop_rect; i.pixel_width = found->pixel_width; i.pixel_height = found->pixel_height;
        // Frame scale maps desktop units to image pixels, not the UI scaling preference.
        i.scale_x = i.scale_y = 1; i.rotation = found->rotation; i.valid_fields = found->valid_fields;
        i.monitor = *found; i.valid_fields |= MAA_WS_VALID_MONITOR;
        copy_text(i.source_key, found->monitor_key); return true;
    }
    HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(t.window_id));
    DWORD pid = 0;
    if (!IsWindow(hwnd) || !GetWindowThreadProcessId(hwnd, &pid)) { e = capture_error(MAA_WS_TARGET_NOT_FOUND, MAA_WS_STAGE_CAPTURE, "Window no longer exists"); return false; }
    if (t.process_id && t.process_id != pid) { e = capture_error(MAA_WS_TARGET_CHANGED, MAA_WS_STAGE_CAPTURE, "Window process differs from requested process"); return false; }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    FILETIME created {}, exited {}, kernel {}, user {};
    if (!process || !GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        const auto code = GetLastError(); if (process) CloseHandle(process);
        e = capture_error(code == ERROR_ACCESS_DENIED ? MAA_WS_PERMISSION_DENIED : MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CAPTURE, "Cannot verify window process", 0, code, "Win32"); return false;
    }
    CloseHandle(process);
    uint64_t value = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    if (identity && identity != value) { e = capture_error(MAA_WS_TARGET_CHANGED, MAA_WS_STAGE_CAPTURE, "Window process was replaced"); return false; }
    identity = value;
    RECT r {}; POINT origin {};
    if (!GetClientRect(hwnd, &r) || !ClientToScreen(hwnd, &origin) || r.right <= 0 || r.bottom <= 0) {
        e = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CAPTURE, "Window has no capturable client geometry", 0, GetLastError(), "Win32"); return false;
    }
    i.native_id = t.window_id; i.process_id = pid;
    i.desktop_rect = { double(origin.x), double(origin.y), double(r.right), double(r.bottom) };
    i.pixel_width = r.right; i.pixel_height = r.bottom; i.scale_x = i.scale_y = 1;
    i.valid_fields = MAA_WS_VALID_IDENTITY | MAA_WS_VALID_GEOMETRY | MAA_WS_VALID_SCALE;
    HMONITOR reference = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
    for (const auto& m : snapshot->items) if (m.native_id == reinterpret_cast<uintptr_t>(reference)) {
        if (m.identity_scope != MAA_WS_IDENTITY_AMBIGUOUS) { i.monitor = m; i.valid_fields |= MAA_WS_VALID_MONITOR; }
        break;
    }
    copy_text(i.source_key, "win32:window:" + std::to_string(t.window_id) + ":" + std::to_string(pid) + ":" + std::to_string(identity)); return true;
#else
    e = capture_error(MAA_WS_UNSUPPORTED_OPTION, MAA_WS_STAGE_CAPTURE, "Unsupported source on this platform"); return false;
#endif
}
#endif
}
namespace mws {
bool TargetScreencapBase::validate_source() {
#if defined(_WIN32)
    auto old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct Restore { DPI_AWARENESS_CONTEXT old; ~Restore() { if (old) SetThreadDpiAwarenessContext(old); } } restore { old };
    if (!old) { error_ = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CAPTURE, "Cannot enter physical pixel DPI context", method_, GetLastError(), "Win32"); return false; }
#endif
    const bool valid = detail::resolve_source(target_, source_, process_identity_, error_);
#if defined(_WIN32)
    // Preflight may precede pseudo-minimize restoration. Process identity was
    // already checked before the geometry query; capture itself still requires
    // fully restored, verified client geometry.
    if (!valid && error_.code == MAA_WS_UNSUPPORTED_GEOMETRY && target_.type == MAA_WS_TARGET_WINDOW
        && IsIconic(reinterpret_cast<HWND>(static_cast<uintptr_t>(target_.window_id)))) return true;
#endif
    return valid;
}
std::optional<CaptureFrame> TargetScreencapBase::screencap() {
#if defined(_WIN32)
    auto old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct Restore { DPI_AWARENESS_CONTEXT old; ~Restore() { if (old) SetThreadDpiAwarenessContext(old); } } restore { old };
    if (!old) { error_ = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CAPTURE, "Cannot enter physical pixel DPI context", method_, GetLastError(), "Win32"); return {}; }
#endif
    error_ = capture_error(MAA_WS_OK, 0, "");
    if (!detail::resolve_source(target_, source_, process_identity_, error_)) { error_.method = method_; return {}; }
    const auto before = source_;
    if (method_ == MAA_WS_METHOD_SCREENDC || method_ == MAA_WS_METHOD_DXGI_DESKTOP_DUP_WINDOW) {
        const auto& w = source_.desktop_rect;
        const auto& r = source_.monitor.desktop_rect;
        if (!(source_.valid_fields & MAA_WS_VALID_MONITOR) || w.x < r.x || w.y < r.y || w.x+w.width > r.x+r.width || w.y+w.height > r.y+r.height) {
            error_ = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CAPTURE, "Visible window capture requires one unambiguous containing output", method_); return {};
        }
        source_.limitations |= MAA_WS_LIMIT_VISIBLE_DESKTOP;
    }
    auto image = capture_image();
    if (!image || image->empty()) {
        if (!error_.code) error_ = capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CAPTURE, "Native backend failed to capture", method_);
        return {};
    }
    MaaWSFrameInfo after {};
    if (!detail::resolve_source(target_, after, process_identity_, error_) || !detail::same_source(before, after, error_)) { error_.method = method_; return {}; }
    if (source_.pixel_semantics == MAA_WS_PIXELS_NATIVE && (uint32_t(image->cols) != source_.pixel_width || uint32_t(image->rows) != source_.pixel_height)) {
        error_ = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CAPTURE, "Buffer dimensions differ from native content geometry", method_); return {};
    }
    source_.layout_after = after.layout_before; source_.method = method_;
    if (target_.type == MAA_WS_TARGET_WINDOW && method_ != MAA_WS_METHOD_SCREENDC && method_ != MAA_WS_METHOD_DXGI_DESKTOP_DUP_WINDOW) {
        source_.monitor = {}; source_.valid_fields &= ~MAA_WS_VALID_MONITOR;
    }
    source_.pixel_width = image->cols; source_.pixel_height = image->rows;
    source_.content_rect = { 0, 0, double(image->cols), double(image->rows) };
    source_.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return CaptureFrame { std::move(*image), source_ };
}
}
