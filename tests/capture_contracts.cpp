#include "CaptureInternal.hpp"
#include "maa-capture/MaaWSCustomScreencap.h"
#include <cstdio>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
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
    options.target = CaptureTarget::monitor("missing"); options.methods = MAA_WS_METHOD_GDI;
    auto handle = MaaWSManagerCreate(&options,&e);
    CHECK(!handle && (e.code == MAA_WS_METHOD_UNAVAILABLE || e.code == MAA_WS_UNSUPPORTED_OPTION));
    MaaWSFrameInfo a {}, b {}; a.native_id = b.native_id = 1; a.layout_before = 1; b.layout_before = 2;
    CHECK(!detail::same_source(a,b,e) && e.code == MAA_WS_LAYOUT_CHANGED);
    b.native_id = 2; CHECK(!detail::same_source(a,b,e) && e.code == MAA_WS_TARGET_CHANGED);
    std::puts("capture contracts: capabilities, target validation, topology and source changes OK");
}
