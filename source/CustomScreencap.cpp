#include "maa-capture/CustomScreencap.h"
#include "maa-capture/ScreencapMgr.h"
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#ifndef MWS_VERSION
#define MWS_VERSION "unknown"
#endif
#ifndef MWS_COMMIT
#define MWS_COMMIT "unknown"
#endif
namespace mws {
std::optional<CaptureFrame> CustomScreencapBridge::screencap() {
    error_ = capture_error(MAA_WS_OK, 0, "");
    if (!connected_) {
        active_ = true;
        if (callbacks_.connect && !callbacks_.connect(context_)) { error_ = capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CALLBACK, "Provider connect failed"); return {}; }
        connected_ = true;
    }
    struct Release { MaaWSCustomScreencapCallbacks& cb; void* ctx; ~Release() { try { if (cb.release_frame) cb.release_frame(ctx); } catch (...) {} } } release { callbacks_, context_ };
    MaaWSFrameInput input {}; input.struct_size = sizeof(input); input.image.struct_size = sizeof(input.image); input.info.struct_size = sizeof(input.info);
    if (!callbacks_.screencap(context_, &input, &error_)) {
        if (!error_.code || error_.code > MAA_WS_STALE_FRAME || error_.struct_size != sizeof(error_) || !valid_text(error_.message) || !valid_text(error_.native_domain))
            error_ = capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CALLBACK, "Provider capture failed or returned an invalid error");
        return {};
    }
    const auto& p = input.image;
    const auto& i = input.info;
    if (input.struct_size != sizeof(input) || input.reserved || p.struct_size != sizeof(p) || i.struct_size != sizeof(i) || i.reserved ||
        !valid_text(i.source_key) || i.target_type != MAA_WS_TARGET_CUSTOM || i.scope != scope_ ||
        std::strcmp(i.source_key, key_.c_str()) || (i.valid_fields & ~31u) || (i.valid_fields & valid_fields_) != valid_fields_) {
        error_ = capture_error(MAA_WS_SOURCE_MISMATCH, MAA_WS_STAGE_CALLBACK, "Provider frame does not match its source declaration"); return {};
    }
    if ((i.valid_fields & MAA_WS_VALID_MONITOR) && (i.monitor.struct_size != sizeof(i.monitor) || !valid_text(i.monitor.monitor_key) || !valid_text(i.monitor.name))) {
        error_ = capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CALLBACK, "Invalid provider monitor metadata"); return {};
    }
    const uint64_t row = uint64_t(p.width) * p.channels;
    if (!p.data || !p.width || !p.height || p.width > INT32_MAX || p.height > INT32_MAX ||
        (p.channels != 3 && p.channels != 4) || p.step < row || p.step > SIZE_MAX ||
        (uint64_t(p.height) - 1 > (UINT64_MAX - row) / p.step) ||
        p.data_size < (uint64_t(p.height) - 1) * p.step + row ||
        uint64_t(p.height) * uint64_t(p.width) > SIZE_MAX / 4) {
        error_ = capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CALLBACK, "Invalid pixel dimensions, stride, length or overflow"); return {};
    }
    if (!valid_frame_geometry(i,p.width,p.height)) {
        error_ = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CALLBACK, "Provider content rectangle is invalid"); return {};
    }
    auto view = Image::wrap(p.height, p.width, p.channels, const_cast<uint8_t*>(p.data), size_t(p.step));
    CaptureFrame frame { p.channels == 4 ? bgra_to_bgr(view) : view.clone(), i };
    frame.source.verification = MAA_WS_VERIFY_PROVIDER; frame.source.pixel_semantics = MAA_WS_PIXELS_PROVIDER;
    frame.source.method = 0;
    frame.source.limitations |= MAA_WS_LIMIT_PROVIDER_IDENTITY;
    frame.source.pixel_width = p.width; frame.source.pixel_height = p.height;
    frame.source.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return frame;
}
void CustomScreencapBridge::inactive() {
    if (active_ && callbacks_.inactive) callbacks_.inactive(context_);
    active_ = false;
    connected_ = false;
}
}
namespace {
using namespace mws;
struct ManagerHandleImpl { explicit ManagerHandleImpl(CaptureOptions o) : manager(std::move(o)) {} ScreencapMgr manager; };
template <typename T> bool sized(T* p) { return p && p->struct_size == sizeof(T); }
void report(MaaWSError* out, MaaWSError value) { if (sized(out)) *out = value; }
MaaWSError internal(uint32_t stage) { return capture_error(MAA_WS_INTERNAL_ERROR, stage, "Native exception at C boundary"); }
ScreencapMgr* manager(MaaWSManagerHandle h) { return h ? &static_cast<ManagerHandleImpl*>(h)->manager : nullptr; }
template <typename F> MaaWSBool guarded(MaaWSManagerHandle h, uint32_t stage, F call) {
    auto* m = manager(h); if (!m) return 0;
    try { return call(*m) ? 1 : 0; } catch (...) { m->fail(internal(stage)); return 0; }
}
}
extern "C" {
uint32_t MaaWSGetAbiVersion() { return MAA_WS_ABI_VERSION; }
uint64_t MaaWSGetApiFeatures() {
    return MAA_WS_FEATURE_FRAMES | MAA_WS_FEATURE_SAMPLE_INFO | MAA_WS_FEATURE_ADVANCED_POLICY | MAA_WS_FEATURE_METHOD_BEHAVIOR
        | MAA_WS_FEATURE_CANCELLATION | MAA_WS_FEATURE_FRAME_FRESHNESS | MAA_WS_FEATURE_DEADLINES
#if defined(_WIN32)
        | MAA_WS_FEATURE_DXGI_CACHE | MAA_WS_FEATURE_STRICT_BACKGROUND
#endif
#if defined(_WIN32) && MWS_FRAMEPOOL_AVAILABLE
        | MAA_WS_FEATURE_FRAMEPOOL_CACHE
#endif
#if defined(_WIN32) || defined(__APPLE__)
        | MAA_WS_FEATURE_MONITORS | MAA_WS_FEATURE_STRICT_SOURCE
#endif
        ;
}
MaaWSBool MaaWSGetRuntimeInfo(MaaWSRuntimeInfo* out) {
    if (!sized(out)) return 0;
    try {
        MaaWSRuntimeInfo i {}; i.struct_size = sizeof(i); i.abi_version = MAA_WS_ABI_VERSION; i.features = MaaWSGetApiFeatures();
        copy_text(i.version, MWS_VERSION); copy_text(i.commit, MWS_COMMIT);
        copy_text(i.platform, current_platform() == 1 ? "windows" : current_platform() == 2 ? "macos" : "linux");
#if defined(_M_ARM64) || defined(__aarch64__)
        copy_text(i.architecture, "arm64");
#elif defined(_M_X64) || defined(__x86_64__)
        copy_text(i.architecture, "x64");
#else
        copy_text(i.architecture, "unknown");
#endif
        *out = i; return 1;
    } catch (...) { return 0; }
}
uint64_t MaaWSManagerGetAvailableMethods() { return available_builtin_methods(); }
MaaWSBool MaaWSGetMethodCapabilities(uint64_t method, MaaWSMethodCapabilities* out, MaaWSError* error) {
    if (!sized(out)) { report(error, capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CREATE, "Invalid capability output size")); return 0; }
    try { MaaWSError e {}; const auto ok = method_capabilities(method, *out, e); report(error, e); return ok; } catch (...) { report(error, internal(MAA_WS_STAGE_CREATE)); return 0; }
}
MaaWSBool MaaWSGetMethodBehavior(uint64_t method, MaaWSMethodBehavior* out, MaaWSError* error) {
    if (!sized(out)) { report(error, capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CREATE, "Invalid behavior output size")); return 0; }
    try { MaaWSError e {}; const bool ok = method_behavior(method, *out, e); report(error,e); return ok; }
    catch (...) { report(error,internal(MAA_WS_STAGE_CREATE)); return 0; }
}
MaaWSMonitorSnapshotHandle MaaWSMonitorSnapshotCreate(MaaWSError* error) {
    try { MaaWSError e {}; auto s = MonitorSnapshot::create(e); report(error, e); return s ? new MonitorSnapshot(std::move(*s)) : nullptr; }
    catch (...) { report(error, internal(MAA_WS_STAGE_ENUMERATE)); return nullptr; }
}
void MaaWSMonitorSnapshotDestroy(MaaWSMonitorSnapshotHandle h) { try { delete static_cast<MonitorSnapshot*>(h); } catch (...) {} }
MaaWSBool MaaWSMonitorSnapshotGetCount(MaaWSMonitorSnapshotHandle h, uint32_t* count, uint64_t* token) {
    if (!h || !count || !token) return 0;
    auto& s = *static_cast<MonitorSnapshot*>(h); *count = uint32_t(s.items.size()); *token = s.layout_token; return 1;
}
MaaWSBool MaaWSMonitorSnapshotGetInfo(MaaWSMonitorSnapshotHandle h, uint32_t index, MaaWSMonitorInfo* out) {
    if (!h || !sized(out)) return 0;
    auto& s = *static_cast<MonitorSnapshot*>(h); if (index >= s.items.size()) return 0; *out = s.items[index]; return 1;
}
MaaWSManagerHandle MaaWSManagerCreate(const MaaWSCaptureOptions* options, MaaWSError* error) {
    const MaaWSCapturePolicy policy {sizeof(MaaWSCapturePolicy), 0, 0};
    return MaaWSManagerCreateWithPolicy(options, &policy, error);
}
MaaWSManagerHandle MaaWSManagerCreateWithPolicy(const MaaWSCaptureOptions* options, const MaaWSCapturePolicy* policy, MaaWSError* error) {
    if (!sized(options) || options->reserved) { report(error, capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CREATE, "Invalid capture options size or reserved field")); return nullptr; }
    if (!sized(policy) || policy->reserved || (policy->flags & ~MAA_WS_POLICY_FRAMEPOOL_CACHE)) {
        report(error, capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CREATE, "Invalid capture policy size, flags or reserved field")); return nullptr;
    }
    try {
        CaptureOptions o { CaptureTarget(options->target), options->methods, policy->flags }; MaaWSError e {};
        if (!validate_options(o, e)) { report(error, e); return nullptr; }
        auto h = std::make_unique<ManagerHandleImpl>(o); report(error, capture_error(MAA_WS_OK, 0, "")); return h.release();
    } catch (...) { report(error, internal(MAA_WS_STAGE_CREATE)); return nullptr; }
}
void MaaWSManagerDestroy(MaaWSManagerHandle h) { try { delete static_cast<ManagerHandleImpl*>(h); } catch (...) {} }
MaaWSManagerHandle MaaWSManagerCreateAdvanced(const MaaWSCaptureOptions* options, const MaaWSAdvancedCapturePolicy* policy, MaaWSError* error) {
    if (!sized(options) || options->reserved || !sized(policy) || policy->reserved) {
        report(error,capture_error(MAA_WS_INVALID_ARGUMENT,MAA_WS_STAGE_CREATE,"Invalid advanced options or policy structure")); return nullptr;
    }
    try {
        CaptureOptions o {CaptureTarget(options->target),options->methods,policy->flags,policy->max_sample_age_ns,
            policy->connect_timeout_ns,policy->capture_timeout_ns,policy->first_frame_timeout_ns};
        MaaWSError e {};
        if (!validate_options(o,e)) { report(error,e); return nullptr; }
        auto h = std::make_unique<ManagerHandleImpl>(o); report(error,capture_error(MAA_WS_OK,0,"")); return h.release();
    } catch (...) { report(error,internal(MAA_WS_STAGE_CREATE)); return nullptr; }
}
MaaWSCancelHandle MaaWSManagerGetCancellationHandle(MaaWSManagerHandle h) {
    try { return h ? new std::shared_ptr<CaptureCancellation>(manager(h)->cancellation_handle()) : nullptr; }
    catch (...) { return nullptr; }
}
MaaWSBool MaaWSCancelRequest(MaaWSCancelHandle h) {
    return h && (*static_cast<std::shared_ptr<CaptureCancellation>*>(h))->request();
}
void MaaWSCancelHandleDestroy(MaaWSCancelHandle h) { delete static_cast<std::shared_ptr<CaptureCancellation>*>(h); }
MaaWSBool MaaWSManagerRegisterCustomUnit(MaaWSManagerHandle h, const char* key, uint32_t scope, uint32_t valid, const MaaWSCustomScreencapCallbacks* cb, void* ctx) {
    return guarded(h, MAA_WS_STAGE_CREATE, [&](auto& m) {
        if (!key || !sized(cb) || cb->reserved || !cb->screencap) return m.fail(capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CREATE, "Invalid callback table or key"));
        const auto length = std::char_traits<char>::length(key);
        if (length >= MAA_WS_KEY_CAPACITY) return m.fail(capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CREATE, "Source key exceeds capacity"));
        return m.register_unit(key, scope, valid, std::make_shared<CustomScreencapBridge>(key, scope, valid, *cb, ctx));
    });
}
MaaWSBool MaaWSManagerConnect(MaaWSManagerHandle h) { return guarded(h, MAA_WS_STAGE_CONNECT, [](auto& m) { return m.connect(); }); }
MaaWSBool MaaWSManagerScreencap(MaaWSManagerHandle h, MaaWSFrameHandle* out) {
    if (out) *out = nullptr;
    return guarded(h, MAA_WS_STAGE_CAPTURE, [&](auto& m) {
        if (!out) return m.fail(capture_error(MAA_WS_INVALID_ARGUMENT, MAA_WS_STAGE_CAPTURE, "Frame output is null"));
        auto f = m.screencap(); if (!f) return false; *out = new CaptureFrame(std::move(*f)); return true;
    });
}
MaaWSBool MaaWSManagerGetLastError(MaaWSManagerHandle h, MaaWSError* error) { if (!h || !sized(error)) return 0; *error = manager(h)->last_error(); return 1; }
const char* MaaWSManagerSelectedUnitName(MaaWSManagerHandle h) { if (!h || !manager(h)->connected()) return nullptr; return manager(h)->selected_unit_name().c_str(); }
void MaaWSManagerInactive(MaaWSManagerHandle h) { guarded(h, MAA_WS_STAGE_CAPTURE, [](auto& m) { m.inactive(); return true; }); }
MaaWSBool MaaWSFrameGetImage(MaaWSFrameHandle h, MaaWSImageView* out) {
    if (!h || !sized(out)) return 0;
    const auto& i = static_cast<CaptureFrame*>(h)->image;
    *out = { sizeof(*out), uint32_t(i.cols), uint32_t(i.rows), uint32_t(i.channels()), i.step, uint64_t(i.rows - 1) * i.step + uint64_t(i.cols) * i.channels(), i.data }; return 1;
}
MaaWSBool MaaWSFrameGetInfo(MaaWSFrameHandle h, MaaWSFrameInfo* out) { if (!h || !sized(out)) return 0; *out = static_cast<CaptureFrame*>(h)->source; return 1; }
MaaWSBool MaaWSFrameGetSampleInfo(MaaWSFrameHandle h, MaaWSFrameSampleInfo* out) { if (!h || !sized(out)) return 0; *out = static_cast<CaptureFrame*>(h)->sample; return 1; }
void MaaWSFrameDestroy(MaaWSFrameHandle h) { try { delete static_cast<CaptureFrame*>(h); } catch (...) {} }
}
