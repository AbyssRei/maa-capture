#include "maa-capture/ScreencapMgr.h"
#include <chrono>
#if defined(_WIN32)
#include "maa-capture/Windows/DesktopDupScreencap.h"
#include "maa-capture/Windows/DesktopDupWindowScreencap.h"
#include "maa-capture/Windows/GdiScreencap.h"
#include "maa-capture/Windows/FramePoolWithPseudoMinimizeScreencap.h"
#include "maa-capture/Windows/PrintWindowWithPseudoMinimizeScreencap.h"
#include "maa-capture/Windows/ScreenDCScreencap.h"
#elif defined(__APPLE__)
#include "maa-capture/MacOS/ScreenCaptureKitScreencap.h"
#else
#if MWS_PIPEWIRE_AVAILABLE
#include "maa-capture/Linux/PipeWireScreencap.h"
#endif
#if MWS_WLR_AVAILABLE
#include "maa-capture/Linux/WlrScreencap.h"
#endif
#endif
namespace mws {
ScreencapMethod available_builtin_methods() {
#if defined(_WIN32)
    return Method_GDI | Method_DXGI_DesktopDup | Method_DXGI_DesktopDup_Window | Method_PrintWindow | Method_ScreenDC
#if MWS_FRAMEPOOL_AVAILABLE
        | Method_FramePool
#endif
        ;
#elif defined(__APPLE__)
    return Method_ScreenCaptureKit;
#else
    return 0
#if MWS_PIPEWIRE_AVAILABLE
        | Method_PipeWire
#endif
#if MWS_WLR_AVAILABLE
        | Method_Wlr
#endif
        ;
#endif
}
const char* method_name(uint64_t m) {
    switch (m) {
    case Method_GDI: return "GDI";
    case Method_FramePool: return "FramePool";
    case Method_DXGI_DesktopDup: return "DXGI_DesktopDup";
    case Method_DXGI_DesktopDup_Window: return "DXGI_DesktopDup_Window";
    case Method_PrintWindow: return "PrintWindow";
    case Method_ScreenDC: return "ScreenDC";
    case Method_PipeWire: return "PipeWire";
    case Method_Wlr: return "Wlr";
    case Method_ScreenCaptureKit: return "ScreenCaptureKit";
    default: return "Custom";
    }
}
ScreencapMgr::ScreencapMgr(CaptureOptions options) : options_(std::move(options)) {
    if (!validate_options(options_, error_)) throw std::invalid_argument(error_.message);
}
ScreencapMgr::~ScreencapMgr() { try { inactive(); } catch (...) {} }
bool ScreencapMgr::register_unit(std::string key, uint32_t scope, uint32_t valid_fields, std::shared_ptr<ScreencapBase> unit) {
    if (connected_ || active_) return fail(capture_error(MAA_WS_INVALID_STATE, MAA_WS_STAGE_CREATE, "Cannot register after connection"));
    if (options_.target.type != MAA_WS_TARGET_CUSTOM || key != options_.target.source_key || scope != target_scope(options_.target) ||
        !unit || units_.contains(key) || (valid_fields & ~31u))
        return fail(capture_error(MAA_WS_SOURCE_MISMATCH, MAA_WS_STAGE_CREATE, "Provider must match the custom target and declared scope"));
    units_.emplace(std::move(key), std::move(unit)); custom_valid_fields_ = valid_fields; return true;
}
bool ScreencapMgr::validate_custom_frame(CaptureFrame& frame, uint32_t stage) {
    if (options_.target.type != MAA_WS_TARGET_CUSTOM) return true;
    auto& s = frame.source;
    if (frame.image.empty() || frame.image.channels() != 3 || s.struct_size != sizeof(s) || s.target_type != MAA_WS_TARGET_CUSTOM
        || !valid_text(s.source_key) || std::strcmp(s.source_key,options_.target.source_key) || s.scope != target_scope(options_.target)
        || (s.valid_fields & ~31u) || (s.valid_fields & custom_valid_fields_) != custom_valid_fields_)
        return fail(capture_error(MAA_WS_SOURCE_MISMATCH, stage, "Custom frame differs from the declared source"));
    if (!valid_frame_geometry(s,frame.image.cols,frame.image.rows))
        return fail(capture_error(MAA_WS_UNSUPPORTED_GEOMETRY,stage,"Custom frame has invalid geometry"));
    s.method = 0; s.verification = MAA_WS_VERIFY_PROVIDER; s.pixel_semantics = MAA_WS_PIXELS_PROVIDER;
    s.limitations |= MAA_WS_LIMIT_PROVIDER_IDENTITY;
    s.pixel_width = frame.image.cols; s.pixel_height = frame.image.rows;
    if (!s.timestamp_ns) s.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return true;
}
void ScreencapMgr::build_builtin_units() {
    if (options_.target.type == MAA_WS_TARGET_CUSTOM) return;
    uint64_t enabled = options_.methods;
    if (!enabled) for (uint64_t bit = 1; bit <= Method_ScreenCaptureKit; bit <<= 1) {
        MaaWSMethodCapabilities i {}; MaaWSError e {};
        method_capabilities(bit, i, e);
        const bool matching = options_.target.type == MAA_WS_TARGET_PIPEWIRE ? bit == Method_PipeWire
            : options_.target.type == MAA_WS_TARGET_WLR ? bit == Method_Wlr : (i.scopes & target_scope(options_.target)) != 0;
        if (i.available && matching) enabled |= bit;
    }
    const auto& t = options_.target;
    const auto add = [&](uint64_t flag, auto create) { if ((enabled & flag) && !units_.contains(method_name(flag))) units_[method_name(flag)] = create(); };
#if defined(_WIN32)
    add(Method_GDI, [&] { return std::make_shared<GdiScreencap>(t); });
    add(Method_FramePool, [&] { return std::make_shared<FramePoolWithPseudoMinimizeScreencap>(t); });
    add(Method_DXGI_DesktopDup, [&] { return std::make_shared<DesktopDupScreencap>(t); });
    add(Method_DXGI_DesktopDup_Window, [&] { return std::make_shared<DesktopDupWindowScreencap>(t); });
    add(Method_PrintWindow, [&] { return std::make_shared<PrintWindowWithPseudoMinimizeScreencap>(t); });
    add(Method_ScreenDC, [&] { return std::make_shared<ScreenDCScreencap>(t); });
#elif defined(__APPLE__)
    add(Method_ScreenCaptureKit, [&] { return std::make_shared<ScreenCaptureKitScreencap>(t); });
#else
#if MWS_PIPEWIRE_AVAILABLE
    add(Method_PipeWire, [&] { return std::make_shared<PipeWireScreencap>(t); });
#endif
#if MWS_WLR_AVAILABLE
    if ((enabled & Method_Wlr) && !units_.contains("Wlr")) {
        auto client = std::make_shared<WaylandClient>();
        if (client->connect(t.socket_name)) units_["Wlr"] = std::make_shared<WlrScreencap>(client, t);
        else fail(capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CONNECT, "Cannot connect to Wayland socket"));
    }
#endif
#endif
}
bool ScreencapMgr::connect() {
    inactive();
    build_builtin_units();
    if (units_.empty()) return fail(capture_error(MAA_WS_METHOD_UNAVAILABLE, MAA_WS_STAGE_CONNECT, "No matching capture provider"));
    active_ = true;
    auto best = std::chrono::steady_clock::duration::max();
    try {
        for (auto& [name, unit] : units_) {
            unit->screencap(); // warm up
            const auto start = std::chrono::steady_clock::now();
            auto frame = unit->screencap();
            if (!frame) { error_ = unit->last_error(); continue; }
            if (!validate_custom_frame(*frame,MAA_WS_STAGE_CONNECT)) continue;
            const auto duration = std::chrono::steady_clock::now() - start;
            if (duration < best) { selected_ = unit; selected_unit_name_ = name; best = duration; }
        }
        for (auto& [name, unit] : units_) if (unit != selected_) unit->inactive();
        if (!selected_) {
            active_ = false;
            if (!error_.code) fail(capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CONNECT, "All capture methods failed"));
            return false;
        }
        connected_ = true; return true;
    } catch (...) { inactive(); throw; }
}
std::optional<CaptureFrame> ScreencapMgr::screencap() {
    if (!connected_ || !selected_) { fail(capture_error(MAA_WS_INVALID_STATE, MAA_WS_STAGE_CAPTURE, "Manager is not connected")); return {}; }
    auto frame = selected_->screencap();
    if (!frame) { error_ = selected_->last_error(); return {}; }
    if (!validate_custom_frame(*frame,MAA_WS_STAGE_CAPTURE)) return {};
    if (options_.target.type == MAA_WS_TARGET_CUSTOM) frame->image = frame->image.clone();
    frame->source.frame_id = ++next_frame_;
    return frame;
}
void ScreencapMgr::inactive() {
    if (active_) for (auto& [name, unit] : units_) { try { unit->inactive(); } catch (...) {} }
    active_ = false; connected_ = false; selected_.reset(); selected_unit_name_.clear();
}
}
