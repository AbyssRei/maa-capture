#include "maa-capture/ScreencapMgr.h"
#include <algorithm>
#include <cstring>
namespace mws {
uint32_t current_platform() {
#if defined(_WIN32)
    return MAA_WS_PLATFORM_WINDOWS;
#elif defined(__APPLE__)
    return MAA_WS_PLATFORM_MACOS;
#else
    return MAA_WS_PLATFORM_LINUX;
#endif
}
uint32_t target_scope(const MaaWSCaptureTarget& t) {
    switch (t.type) {
    case MAA_WS_TARGET_MONITOR: case MAA_WS_TARGET_WLR: return MAA_WS_SCOPE_SCREEN;
    case MAA_WS_TARGET_WINDOW: return MAA_WS_SCOPE_WINDOW;
    case MAA_WS_TARGET_PIPEWIRE: case MAA_WS_TARGET_CUSTOM: return t.scope;
    default: return 0;
    }
}
bool method_capabilities(uint64_t method, MaaWSMethodCapabilities& i, MaaWSError& e) {
    i = {}; i.struct_size = sizeof(i); i.method = method;
    if (!method || (method & (method - 1)) || method > Method_ScreenCaptureKit) {
        e = capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CREATE, "Expected a single known method bit", method); return false;
    }
    i.platforms = method < Method_PipeWire ? MAA_WS_PLATFORM_WINDOWS : method == Method_ScreenCaptureKit ? MAA_WS_PLATFORM_MACOS : MAA_WS_PLATFORM_LINUX;
    i.scopes = method == Method_DXGI_DesktopDup || method == Method_Wlr ? MAA_WS_SCOPE_SCREEN
        : method == Method_PipeWire ? MAA_WS_SCOPE_SOURCE
        : method == Method_ScreenCaptureKit ? MAA_WS_SCOPE_SCREEN | MAA_WS_SCOPE_WINDOW : MAA_WS_SCOPE_WINDOW;
    i.available = (available_builtin_methods() & method) != 0;
    if (method == Method_ScreenDC || method == Method_DXGI_DesktopDup_Window) i.flags = MAA_WS_LIMIT_VISIBLE_DESKTOP;
    e = capture_error(MAA_WS_OK, 0, ""); return true;
}
bool validate_options(const CaptureOptions& o, MaaWSError& e) {
    const auto& t = o.target;
    const auto invalid = [&](const char* text) { e = capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CREATE, text); return false; };
    if (t.struct_size != sizeof(MaaWSCaptureTarget) || t.reserved ||
        !valid_text(t.source_key) || !valid_text(t.socket_name)) return invalid("Invalid target size, reserved field or UTF-8 string");
    if (!target_scope(t) || (target_scope(t) & (target_scope(t) - 1)) || target_scope(t) > MAA_WS_SCOPE_UNKNOWN) return invalid("Invalid target type or scope");
    if (t.platform && t.platform != current_platform()) {
        e = capture_error(MAA_WS_UNSUPPORTED_OPTION, MAA_WS_STAGE_CREATE, "Target platform differs from this runtime"); return false;
    }
    switch (t.type) {
    case MAA_WS_TARGET_MONITOR:
        if (!t.source_key[0] || t.window_id || t.process_id || t.pipewire_fd || t.node_id || t.output_index || t.scope || t.socket_name[0]) return invalid("Monitor requires only a device key");
        if (current_platform() == MAA_WS_PLATFORM_LINUX) { e = capture_error(MAA_WS_UNSUPPORTED_OPTION, MAA_WS_STAGE_CREATE, "Stable monitor targets are unsupported on Linux"); return false; }
        break;
    case MAA_WS_TARGET_WINDOW:
        if (!t.window_id || !t.platform || t.source_key[0] || t.pipewire_fd || t.node_id || t.output_index || t.scope || t.socket_name[0]) return invalid("Window requires platform and native window ID");
        if (current_platform() == MAA_WS_PLATFORM_LINUX || (current_platform() == MAA_WS_PLATFORM_MACOS && t.window_id > UINT32_MAX)) return invalid("Unsupported native window ID");
        break;
    case MAA_WS_TARGET_CUSTOM:
        if (!t.source_key[0] || t.window_id || t.process_id || t.pipewire_fd || t.node_id || t.output_index || t.socket_name[0]) return invalid("Custom requires source key and declared scope");
        if (o.methods) return invalid("Custom targets do not accept builtin method bits");
        return true;
    case MAA_WS_TARGET_PIPEWIRE:
        if (t.window_id || t.process_id || t.source_key[0] || t.output_index || t.socket_name[0] || t.pipewire_fd < -1 || (!t.node_id && t.pipewire_fd < 0)) return invalid("Invalid PipeWire target");
        break;
    case MAA_WS_TARGET_WLR:
        if (t.window_id || t.process_id || t.source_key[0] || t.pipewire_fd || t.node_id || t.scope) return invalid("Invalid Wlr target");
        break;
    default: return invalid("Unknown target type");
    }
    if ((t.type == MAA_WS_TARGET_WLR || t.type == MAA_WS_TARGET_PIPEWIRE) && current_platform() != MAA_WS_PLATFORM_LINUX) {
        e = capture_error(MAA_WS_UNSUPPORTED_OPTION, MAA_WS_STAGE_CREATE, "Linux target is unsupported on this platform"); return false;
    }
    uint64_t candidates = 0;
    for (uint64_t bit = 1; bit <= Method_ScreenCaptureKit; bit <<= 1) {
        MaaWSMethodCapabilities i {};
        method_capabilities(bit, i, e);
        const bool matching = t.type == MAA_WS_TARGET_PIPEWIRE ? bit == Method_PipeWire
            : t.type == MAA_WS_TARGET_WLR ? bit == Method_Wlr : (i.scopes & target_scope(t)) != 0;
        if (i.available && matching) candidates |= bit;
    }
    if ((o.methods && (o.methods & ~candidates)) || !candidates) {
        e = capture_error(MAA_WS_METHOD_UNAVAILABLE, MAA_WS_STAGE_CREATE, "Requested method is unknown, unbuilt, or outside target scope", o.methods); return false;
    }
    e = capture_error(MAA_WS_OK, 0, ""); return true;
}
}
