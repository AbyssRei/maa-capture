#include "CaptureInternal.hpp"
#include "maa-capture/MaaWSCustomScreencap.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <future>
#include <functional>
#include "maa-capture/ScreencapMgr.h"
#include "WindowStateTransaction.hpp"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace mws {
struct ScreencapMgrTestAccess {
    static void add(ScreencapMgr& m, const char* key, std::shared_ptr<ScreencapBase> unit) { m.units_[key] = std::move(unit); }
};
}
struct FakeWindow : mws::detail::WindowStateOps {
    uint64_t style = 8;
    mws::detail::LayeredAttributes attrs {7, 3, 123};
    bool alive = true, minimized = true, restored = false, unreadable = false;
    int alpha_writes = 0, fail_alpha = 0, conflicts = 0, style_writes = 0;
    bool valid() override { return alive; }
    bool read_style(uint64_t& out) override { out = style; return true; }
    bool write_style(uint64_t value) override { ++style_writes; style = value; return true; }
    bool read_layered(mws::detail::LayeredAttributes& out) override { out = attrs; return !unreadable; }
    bool write_layered(const mws::detail::LayeredAttributes& value) override {
        if (++alpha_writes == fail_alpha) return false;
        attrs = value; return true;
    }
    bool show(bool value) override { minimized = value; return true; }
    bool user_restored() override { return restored; }
    bool conflict() override { ++conflicts; return false; }
};
struct FakeUnit : mws::ScreencapBase {
    int& active_count;
    int samples = 0, stops = 0, fail_sample = 0, delay_ms = 0;
    bool active = false, overlap = false, throws = false, pending_valid = true;
    mws::Image pixels {1, 1, 3};
    std::function<void()> entered;
    bool cooperative = false;
    explicit FakeUnit(int& count) : active_count(count) { pixels.data[0] = 42; }
    std::optional<mws::CaptureFrame> screencap() override {
        if (!active) { overlap |= active_count != 0; ++active_count; active = true; }
        ++samples;
        if (entered) entered();
        if (throws) throw std::runtime_error("injected capture failure");
        if (samples == fail_sample) { error_ = mws::capture_error(MAA_WS_TIMEOUT, MAA_WS_STAGE_CAPTURE, "injected timeout"); return {}; }
        if (delay_ms) {
            const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(delay_ms);
            do {
                if (cooperative && operation_ && !operation_->check(error_)) return {};
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while(std::chrono::steady_clock::now()<until);
        }
        MaaWSFrameInfo info {}; info.struct_size = sizeof(info); info.target_type = MAA_WS_TARGET_CUSTOM;
        info.scope = MAA_WS_SCOPE_UNKNOWN; mws::copy_text(info.source_key, "test"); info.timestamp_ns = samples;
        // Provider-supplied native freshness claims must never escape Manager.
        return mws::CaptureFrame {pixels, info, {sizeof(MaaWSFrameSampleInfo),MAA_WS_SAMPLE_NEW,999,999,999,999}};
    }
    bool validate_pending_frame(const mws::CaptureFrame&) override { return pending_valid; }
    void inactive() override { if (active) { --active_count; ++stops; active = false; } }
};
struct ProviderBackend : mws::TargetScreencapBase {
    ProviderBackend() : TargetScreencapBase(mws::CaptureTarget::wlr("test",0),mws::Method_Wlr) {}
    std::optional<mws::Image> capture_image() override {return mws::Image(2,3,3);}
};
static int repair_contracts() {
    using namespace mws;
    using detail::WindowStateTransaction;
    // macOS has no Wlr/PipeWire source resolver. Do not make a provider-only
    // geometry regression depend on a real ScreenCaptureKit desktop there.
#if !defined(__APPLE__)
    {
        ProviderBackend backend;auto frame=backend.screencap();
        CHECK(frame && frame->source.pixel_width==3 && backend.validate_pending_frame(*frame));
    }
#endif
    {
        FakeWindow w; WindowStateTransaction t(1, 2, 2);
        CHECK(t.apply(w) && w.style == 11 && !w.minimized);
        w.style |= 16;
        CHECK(t.restore(w, true) && w.style == 24 && w.minimized);
        CHECK(t.restore(w, true));
    }
    {
        FakeWindow w; w.style |= 1; const auto attributes = w.attrs; WindowStateTransaction t(1, 2, 2);
        CHECK(t.apply(w)); CHECK(t.restore(w, true)); CHECK(w.attrs == attributes && w.style == 9);
        w.fail_alpha = w.alpha_writes + 1;
        CHECK(!t.apply(w) && !t.modified() && w.style == 9 && w.attrs == attributes);
        w.unreadable = true;
        const auto writes = w.style_writes;
        CHECK(!t.apply(w) && w.style_writes == writes);
        w.unreadable = false; w.fail_alpha = 0; CHECK(t.apply(w));
        w.fail_alpha = w.alpha_writes + 1;
        CHECK(!t.restore(w,true) && t.modified());
        CHECK(t.restore(w,true) && w.attrs == attributes && w.style == 9);
    }
    {
        FakeWindow w; WindowStateTransaction t(1, 2, 2); CHECK(t.apply(w));
        w.restored = true; CHECK(t.observe(w)); CHECK(!w.minimized && w.style == 8 && !t.modified());
        w.restored = false; CHECK(t.apply(w)); w.attrs.alpha = 77;
        CHECK(!t.restore(w, true) && w.conflicts == 1 && w.attrs.alpha == 77);
        w.alive = false; const auto writes = w.style_writes;
        CHECK(!t.restore(w, true) && !t.modified() && writes == w.style_writes);
    }
    {
        int active = 0;
        auto unit = std::make_shared<FakeUnit>(active); unit->fail_sample = 2;
        ScreencapMgr manager({CaptureTarget::custom("test")}); ScreencapMgrTestAccess::add(manager, "test", unit);
        CHECK(manager.connect() && unit->samples == 1);
        unit->pixels.data[0] = 99;
        auto frame = manager.screencap(); CHECK(frame && frame->image.data[0] == 42 && frame->source.timestamp_ns == 1);
        CHECK(frame->sample.flags==MAA_WS_SAMPLE_CONNECT_PENDING && frame->sample.sample_id==0 && frame->sample.sampled_at_ns==0);
        CHECK(!manager.screencap()); manager.inactive(); CHECK(active == 0);
        CHECK(manager.connect()); unit->pending_valid = false;
        auto fresh = manager.screencap(); CHECK(fresh && fresh->source.timestamp_ns == 4);
        manager.inactive(); CHECK(!manager.screencap());
    }
    {
        int active = 0;
        auto a = std::make_shared<FakeUnit>(active), b = std::make_shared<FakeUnit>(active), bad = std::make_shared<FakeUnit>(active);
        a->fail_sample = 2; b->delay_ms = 10; bad->throws = true;
        ScreencapMgr manager({CaptureTarget::custom("test")});
        ScreencapMgrTestAccess::add(manager,"a",a); ScreencapMgrTestAccess::add(manager,"b",b); ScreencapMgrTestAccess::add(manager,"c",bad);
        CHECK(manager.connect() && manager.selected_unit_name() == "b");
        CHECK(!a->overlap && !b->overlap && !bad->overlap && active == 1 && a->stops == 2 && bad->stops == 1);
        auto frame = manager.screencap(); CHECK(frame && frame->source.timestamp_ns == 2 && b->samples == 2);
        manager.inactive(); CHECK(active == 0);
    }
    return 0;
}
static int enhancement_contracts() {
    using namespace mws;
    MaaWSError error{};CaptureOperation freshness;
    CaptureFrame frame{Image(1,1,3),{}};frame.sample.sampled_at_ns=capture_clock_ns();
    freshness.policy_flags=MAA_WS_POLICY_FRESH_ONLY;
    CHECK(freshness.accepts(frame,error));
    frame.sample.flags=MAA_WS_SAMPLE_REUSED;
    CHECK(!freshness.accepts(frame,error)&&error.code==MAA_WS_STALE_FRAME);
    freshness.policy_flags=0;freshness.max_sample_age_ns=1;frame.sample.sampled_at_ns=1;
    CHECK(!freshness.accepts(frame,error)&&error.code==MAA_WS_STALE_FRAME);
    frame.sample.sampled_at_ns=0;
    CHECK(!freshness.accepts(frame,error)&&error.code==MAA_WS_UNSUPPORTED_OPTION);
    freshness.deadline_ns=0;CHECK(!freshness.check(error)&&error.code==MAA_WS_DEADLINE_EXCEEDED);
    {
        int active=0;auto unit=std::make_shared<FakeUnit>(active);
        CaptureOptions options{CaptureTarget::custom("test")};options.capture_timeout_ns=5000000;
        ScreencapMgr manager(options);ScreencapMgrTestAccess::add(manager,"test",unit);
        CHECK(manager.connect()&&manager.screencap());unit->delay_ms=20;
        CHECK(!manager.screencap()&&manager.last_error().code==MAA_WS_DEADLINE_EXCEEDED&&!manager.connected()&&active==0);
    }
    {
        int active=0;auto a=std::make_shared<FakeUnit>(active),b=std::make_shared<FakeUnit>(active);
        a->delay_ms=b->delay_ms=30;
        CaptureOptions options{CaptureTarget::custom("test")};options.connect_timeout_ns=40000000;
        ScreencapMgr manager(options);ScreencapMgrTestAccess::add(manager,"a",a);ScreencapMgrTestAccess::add(manager,"b",b);
        CHECK(!manager.connect()&&manager.last_error().code==MAA_WS_DEADLINE_EXCEEDED&&active==0);
    }
    {
        int active=0;auto unit=std::make_shared<FakeUnit>(active);unit->delay_ms=20;
        CaptureOptions options{CaptureTarget::custom("test")};options.first_frame_timeout_ns=1000000;
        ScreencapMgr manager(options);ScreencapMgrTestAccess::add(manager,"test",unit);
        CHECK(!manager.connect()&&manager.last_error().code==MAA_WS_DEADLINE_EXCEEDED&&active==0);
    }
    std::shared_ptr<CaptureCancellation> retained;
    {
        int active=0;auto unit=std::make_shared<FakeUnit>(active);unit->delay_ms=1000;unit->cooperative=true;
        ScreencapMgr manager({CaptureTarget::custom("test")});ScreencapMgrTestAccess::add(manager,"test",unit);
        retained=manager.cancellation_handle();CHECK(!retained->request());
        std::promise<void> entered;auto started=entered.get_future();unit->entered=[&]{entered.set_value();};
        auto connected=std::async(std::launch::async,[&]{return manager.connect();});
        started.wait();CHECK(retained->request());CHECK(!connected.get()&&manager.last_error().code==MAA_WS_CANCELLED&&active==0);
        unit->entered={};unit->delay_ms=0;CHECK(manager.connect()&&manager.screencap());
        CHECK(!retained->request()); // idle request does not poison a later call
    }
    CHECK(!retained->request());
    MaaWSMethodBehavior behavior{};behavior.struct_size=sizeof(behavior);
    CHECK(MaaWSGetMethodBehavior(Method_PrintWindow,&behavior,&error)&&(behavior.flags&MAA_WS_BEHAVIOR_NO_ACTIVATE)&&(behavior.flags&MAA_WS_BEHAVIOR_UNCANCELLABLE_CALLS));
    CHECK(MaaWSGetMethodBehavior(Method_ScreenDC,&behavior,&error)&&(behavior.flags&MAA_WS_BEHAVIOR_MAY_ACTIVATE));
    CHECK(!MaaWSGetMethodBehavior(3,&behavior,&error)&&error.code==MAA_WS_INVALID_ARGUMENT);
    static_assert(sizeof(MaaWSCapturePolicy)==16&&sizeof(MaaWSAdvancedCapturePolicy)==48);
    CHECK(!MaaWSManagerGetCancellationHandle(nullptr)&&!MaaWSCancelRequest(nullptr));MaaWSCancelHandleDestroy(nullptr);
    return 0;
}
int main() {
    CHECK(repair_contracts() == 0);
    CHECK(enhancement_contracts() == 0);
    using namespace mws;
    MaaWSError e {}; e.struct_size = sizeof(e);
    MaaWSMethodCapabilities c {}; c.struct_size = sizeof(c);
    CHECK(MaaWSGetMethodCapabilities(MAA_WS_METHOD_SCREENDC, &c, &e) && c.scopes == MAA_WS_SCOPE_WINDOW);
    CHECK(!MaaWSGetMethodCapabilities(3, &c, &e) && e.code == MAA_WS_INVALID_ARGUMENT);
    CHECK(!MaaWSGetMethodCapabilities(1ULL<<63, &c, &e));
    auto t = CaptureTarget::custom("test");
    CHECK(validate_options({t,0},e)); CHECK(!validate_options({t,1},e));
    t.window_id = 1; CHECK(!validate_options({t,0},e));
    MaaWSMonitorInfo m {}; m.struct_size = sizeof(m); m.native_id = 42; m.primary = 1;
    m.identity_scope = MAA_WS_IDENTITY_PERSISTENT; m.desktop_rect = {-1920,-100,1920,1080};
    m.pixel_width = 1920; m.pixel_height = 1080; m.scale_x = m.scale_y = 1.25; m.rotation = 90;
    copy_text(m.name,"same name"); copy_text(m.monitor_key,"test:a");
    auto n = m; n.primary = 0; n.native_id = 43; copy_text(n.monitor_key,"test:b");
    int calls = 0;
    auto provider = [&](MaaWSError&) -> std::optional<std::vector<MaaWSMonitorInfo>> { ++calls; return calls % 2 ? std::vector{m,n} : std::vector{n,m}; };
    auto s = detail::snapshot_from(provider,e);
    CHECK(s && calls == 2 && s->items.size() == 2 && s->items[0].native_id == 42 && s->items[0].desktop_rect.x == -1920);
    const auto original = s->layout_token;
    m.scale_x = 2;
    CHECK(s->items[0].scale_x == 1.25); // immutable copy
    auto changed = detail::snapshot_from(provider,e); CHECK(changed && changed->layout_token != original);
    // Explicit empty vector must be distinguishable from provider failure.
    auto zero = detail::snapshot_from([](auto&) -> std::optional<std::vector<MaaWSMonitorInfo>> { return std::vector<MaaWSMonitorInfo>{}; },e);
    CHECK(zero && zero->items.empty());
    n.native_id = m.native_id;
    auto ambiguous = detail::snapshot_from(provider,e); CHECK(ambiguous && ambiguous->items[0].identity_scope == MAA_WS_IDENTITY_AMBIGUOUS);
    auto unstable = detail::snapshot_from([&](auto&) -> std::optional<std::vector<MaaWSMonitorInfo>> { m.desktop_rect.x += 1; return std::vector{m}; },e);
    CHECK(!unstable && e.code == MAA_WS_LAYOUT_CHANGED);
    auto failure = detail::snapshot_from([](auto& error) -> std::optional<std::vector<MaaWSMonitorInfo>> { error = capture_error(MAA_WS_PERMISSION_DENIED, MAA_WS_STAGE_ENUMERATE, "test"); return {}; },e);
    CHECK(!failure && e.code == MAA_WS_PERMISSION_DENIED);
    Image pixels(2,3,3);
    CHECK(pixels.roi(-1,0,2,1).empty());
    CHECK(pixels.roi(1,1,INT32_MAX,1).empty());
    CHECK(Image::wrap(2,3,4,pixels.data,3).empty());
    auto invalid = CaptureTarget::custom("test");
    invalid.source_key[0] = char(0xff);
    CHECK(!validate_options({invalid,0},e) && e.code == MAA_WS_INVALID_ARGUMENT);
    MaaWSCaptureOptions options {}; options.struct_size = sizeof(options);
    static_assert(sizeof(MaaWSCaptureOptions)==832 && sizeof(MaaWSFrameInfo)==1584);
    MaaWSCapturePolicy policy {sizeof(policy),0,2};
    CHECK(!MaaWSManagerCreateWithPolicy(&options,&policy,&e) && e.code==MAA_WS_INVALID_ARGUMENT);
    CHECK(!MaaWSManagerCreateWithPolicy(&options,nullptr,&e) && e.code==MAA_WS_INVALID_ARGUMENT);
    CHECK(!MaaWSFrameGetSampleInfo(nullptr,nullptr));
    options.target = CaptureTarget::monitor("missing"); options.methods = MAA_WS_METHOD_GDI;
    auto handle = MaaWSManagerCreate(&options,&e);
    CHECK(!handle && (e.code == MAA_WS_METHOD_UNAVAILABLE || e.code == MAA_WS_UNSUPPORTED_OPTION));
    MaaWSFrameInfo a {}, b {}; a.native_id = b.native_id = 1; a.layout_before = 1; b.layout_before = 2;
    CHECK(!detail::same_source(a,b,e) && e.code == MAA_WS_LAYOUT_CHANGED);
    b.native_id = 2; CHECK(!detail::same_source(a,b,e) && e.code == MAA_WS_TARGET_CHANGED);
    std::puts("capture contracts: capabilities, target validation, topology and source changes OK");
}
