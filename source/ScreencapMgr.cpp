#include "maa-capture/ScreencapMgr.h"
#include <chrono>
#include <algorithm>
#include "maa-capture/Logger.h"
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
std::shared_ptr<CaptureOperation> ScreencapMgr::begin_operation(uint32_t stage) {
    auto operation = std::make_shared<CaptureOperation>();
    operation->stage = stage; operation->policy_flags = options_.policy_flags; operation->max_sample_age_ns = options_.max_sample_age_ns;
    operation->limit(stage == MAA_WS_STAGE_CONNECT ? options_.connect_timeout_ns : options_.capture_timeout_ns);
    if (stage == MAA_WS_STAGE_CONNECT) operation->limit(options_.first_frame_timeout_ns);
    operation->cancellation = cancellation_; operation_epoch_ += 2;
    if (!operation_epoch_) operation_epoch_ = 2;
    operation->epoch = operation_epoch_; cancellation_->state_.store(operation_epoch_);
    return operation;
}
void ScreencapMgr::end_operation() noexcept {
    cancellation_->state_.store(0);
    for (auto& [name,unit] : units_) {
        try { unit->set_operation(nullptr); }
        catch (...) { error_ = capture_error(MAA_WS_INTERNAL_ERROR,MAA_WS_STAGE_CAPTURE,"Provider operation cleanup threw an exception"); }
    }
}
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
    // Custom providers cannot certify native sampling or content freshness.
    frame.sample = {sizeof(MaaWSFrameSampleInfo), MAA_WS_SAMPLE_UNKNOWN, 0, 0, 0, 0};
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
    const auto add = [&](uint64_t flag, auto create) {
        if (!(enabled & flag) || units_.contains(method_name(flag))) return;
        if (options_.policy_flags & MAA_WS_POLICY_STRICT_BACKGROUND) {
            MaaWSMethodBehavior behavior {}; MaaWSError error {}; method_behavior(flag,behavior,error);
            if (!(behavior.flags & MAA_WS_BEHAVIOR_NO_ACTIVATE)) {
                if (options_.policy_flags & MAA_WS_POLICY_DIAGNOSTICS) LogDebug << "event=candidate_rejected method=" << flag << " reason=may_activate";
                return;
            }
        }
        units_[method_name(flag)] = create();
    };
#if defined(_WIN32)
    add(Method_GDI, [&] { return std::make_shared<GdiScreencap>(t); });
    add(Method_FramePool, [&] { return std::make_shared<FramePoolWithPseudoMinimizeScreencap>(t, bool(options_.policy_flags & MAA_WS_POLICY_FRAMEPOOL_CACHE)); });
    add(Method_DXGI_DesktopDup, [&] { return std::make_shared<DesktopDupScreencap>(t,Method_DXGI_DesktopDup,bool(options_.policy_flags & MAA_WS_POLICY_DXGI_CACHE)); });
    add(Method_DXGI_DesktopDup_Window, [&] { return std::make_shared<DesktopDupWindowScreencap>(t,bool(options_.policy_flags & MAA_WS_POLICY_DXGI_CACHE)); });
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
    auto operation = begin_operation(MAA_WS_STAGE_CONNECT); OperationGuard operation_guard {*this};
    inactive();
    if (!operation->check(error_)) return false;
    build_builtin_units();
    if (units_.empty()) return fail(capture_error(MAA_WS_METHOD_UNAVAILABLE, MAA_WS_STAGE_CONNECT, "No matching capture provider"));
    active_ = true;
    for (auto& [name,unit] : units_) unit->enable_diagnostics(bool(options_.policy_flags & MAA_WS_POLICY_DIAGNOSTICS));
    struct Candidate { std::string name; std::shared_ptr<ScreencapBase> unit; std::chrono::steady_clock::duration duration; };
    std::vector<Candidate> candidates;
    const auto take = [&](const std::shared_ptr<ScreencapBase>& unit) -> std::optional<CaptureFrame> {
        if (!operation->check(error_)) return {};
        unit->set_operation(operation);
        auto frame = unit->screencap();
        if (!operation->check(error_,frame ? frame->source.method : unit->last_error().method)) return {};
        if (!frame) { error_ = unit->last_error(); return {}; }
        if (!validate_custom_frame(*frame, MAA_WS_STAGE_CONNECT)) return {};
        // Every retained first frame owns pixels even for a C++ provider view.
        frame->image = frame->image.clone();
        if (!operation->accepts(*frame,error_)) return {};
        return frame;
    };
    const auto cleanup = [&](const std::shared_ptr<ScreencapBase>& unit) {
        try { if (!unit->release_resources()) { error_ = unit->last_error(); return false; } }
        catch (...) { error_ = capture_error(MAA_WS_INTERNAL_ERROR, MAA_WS_STAGE_CONNECT, "Candidate cleanup threw an exception"); return false; }
        return true;
    };
    try {
        // Fixed methods and a single custom provider do not need benchmarking
        // or an extra activation that could lose their only valid first frame.
        if (units_.size() == 1) {
            auto& [name, unit] = *units_.begin();
            auto frame = take(unit);
            if (!frame) { inactive(); return false; }
            selected_ = unit; selected_unit_name_ = name; pending_frame_ = std::move(frame);
            connected_ = true; return true;
        }
        for (auto& [name, unit] : units_) {
            const auto start = std::chrono::steady_clock::now();
            std::optional<CaptureFrame> frame;
            try { frame = take(unit); }
            catch (...) { error_ = capture_error(MAA_WS_INTERNAL_ERROR, MAA_WS_STAGE_CONNECT, "Candidate capture threw an exception"); }
            const auto duration = std::chrono::steady_clock::now() - start;
            LogDebug << "candidate=" << name << " sample_ns=" << std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()
                << " samples=1 phase=cold valid=" << bool(frame) << " error=" << (frame ? 0 : error_.code)
                << " stage=" << (frame ? 0 : error_.stage) << " native=" << (frame ? 0 : error_.native_code);
            if (!cleanup(unit)) { inactive(); return false; }
            if (!operation->check(error_)) { inactive(); return false; }
            if (frame) candidates.push_back({name, unit, duration});
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.duration < b.duration; });
        for (const auto& candidate : candidates) {
            std::optional<CaptureFrame> frame;
            try { frame = take(candidate.unit); }
            catch (...) { error_ = capture_error(MAA_WS_INTERNAL_ERROR, MAA_WS_STAGE_CONNECT, "Candidate activation threw an exception"); }
            if (frame) {
                selected_ = candidate.unit; selected_unit_name_ = candidate.name;
                pending_frame_ = std::move(frame); connected_ = true; return true;
            }
            if (!cleanup(candidate.unit)) { inactive(); return false; }
            if (!operation->check(error_)) { inactive(); return false; }
        }
        inactive();
        if (!error_.code) fail(capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CONNECT, "All capture methods failed"));
        return false;
    } catch (...) { inactive(); if (!operation->check(error_)) return false; throw; }
}
std::optional<CaptureFrame> ScreencapMgr::screencap() {
    if (!connected_ || !selected_) { fail(capture_error(MAA_WS_INVALID_STATE, MAA_WS_STAGE_CAPTURE, "Manager is not connected")); return {}; }
    auto operation = begin_operation(MAA_WS_STAGE_CAPTURE); OperationGuard operation_guard {*this};
    selected_->set_operation(operation);
    try {
    std::optional<CaptureFrame> frame;
    bool connection_frame = false;
    if (pending_frame_) {
        auto pending = std::move(*pending_frame_); pending_frame_.reset();
        MaaWSError freshness {};
        if (selected_->validate_pending_frame(pending) && operation->accepts(pending,freshness)) {
            connection_frame = true;
            frame = std::move(pending);
        }
    }
    if (!frame) {
        if (!operation->check(error_)) { inactive(); return {}; }
        frame = selected_->screencap();
    }
    if (!operation->check(error_,frame ? frame->source.method : selected_->last_error().method)) { inactive(); return {}; }
    if (!frame) { error_ = selected_->last_error(); if (error_.code == MAA_WS_CANCELLED || error_.code == MAA_WS_DEADLINE_EXCEEDED) inactive(); return {}; }
    if (!validate_custom_frame(*frame,MAA_WS_STAGE_CAPTURE)) return {};
    if (connection_frame) frame->sample.flags |= MAA_WS_SAMPLE_CONNECT_PENDING;
    if (options_.target.type == MAA_WS_TARGET_CUSTOM) frame->image = frame->image.clone();
    if (!operation->accepts(*frame,error_)) {
        if (error_.code == MAA_WS_CANCELLED || error_.code == MAA_WS_DEADLINE_EXCEEDED) inactive();
        return {};
    }
    frame->source.frame_id = ++next_frame_;
    frame->sample.delivered_at_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if ((options_.policy_flags & MAA_WS_POLICY_DIAGNOSTICS) && (next_frame_ <= 4 || !(next_frame_ & (next_frame_ - 1))))
        LogDebug << "event=frame_delivered method=" << frame->source.method << " elapsed_ns=" << capture_clock_ns()-operation->started_ns
            << " sample=" << frame->sample.sample_id << " flags=" << frame->sample.flags << " delivery=" << next_frame_;
    return frame;
    } catch (...) { inactive(); if (!operation->check(error_)) return {}; throw; }
}
void ScreencapMgr::inactive() {
    pending_frame_.reset();
    if (active_) for (auto& [name, unit] : units_) {
        try { if (!unit->release_resources()) error_ = unit->last_error(); }
        catch (...) { error_ = capture_error(MAA_WS_INTERNAL_ERROR, MAA_WS_STAGE_CAPTURE, "Capture cleanup threw an exception"); }
    }
    active_ = false; connected_ = false; selected_.reset(); selected_unit_name_.clear();
}
}
