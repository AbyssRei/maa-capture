#include "CaptureInternal.hpp"
#include <algorithm>
#include <cstring>
#include <map>
namespace mws::detail {
static void normalize(std::vector<MaaWSMonitorInfo>& items) {
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        if (a.primary != b.primary) return a.primary > b.primary;
        return std::strcmp(a.monitor_key, b.monitor_key) < 0;
    });
    std::map<std::string, unsigned> keys;
    std::map<uint64_t, unsigned> handles;
    for (auto& i : items) { ++keys[i.monitor_key]; ++handles[i.native_id]; }
    for (uint32_t n = 0; n < items.size(); ++n) {
        auto& i = items[n]; i.struct_size = sizeof(i); i.index = n; i.layout_token = 0;
        if (keys[i.monitor_key] != 1 || handles[i.native_id] != 1) i.identity_scope = MAA_WS_IDENTITY_AMBIGUOUS;
    }
}
static uint64_t token(const std::vector<MaaWSMonitorInfo>& items) {
    uint64_t hash = 1469598103934665603ULL;
    const auto bytes = [&](const void* value, size_t length) {
        auto* p = static_cast<const unsigned char*>(value);
        for (size_t n = 0; n < length; ++n) { hash ^= p[n]; hash *= 1099511628211ULL; }
    };
    for (const auto& i : items) {
        // Hash semantic fields, never unspecified C++ structure padding.
        bytes(&i.identity_scope,sizeof(i.identity_scope)); bytes(&i.primary,sizeof(i.primary));
        bytes(&i.native_id,sizeof(i.native_id)); bytes(&i.desktop_rect,sizeof(i.desktop_rect));
        bytes(&i.coordinate_unit,sizeof(i.coordinate_unit)); bytes(&i.coordinate_space,sizeof(i.coordinate_space));
        bytes(&i.axis,sizeof(i.axis)); bytes(&i.valid_fields,sizeof(i.valid_fields));
        bytes(&i.pixel_width,sizeof(i.pixel_width)); bytes(&i.pixel_height,sizeof(i.pixel_height));
        bytes(&i.rotation,sizeof(i.rotation)); bytes(&i.scale_x,sizeof(i.scale_x)); bytes(&i.scale_y,sizeof(i.scale_y));
        bytes(i.monitor_key,std::strlen(i.monitor_key)+1); bytes(i.name,std::strlen(i.name)+1);
    }
    return hash ? hash : 1;
}
std::optional<MonitorSnapshot> snapshot_from(const MonitorProvider& provider, MaaWSError& error) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto first = provider(error); if (!first) { if (error.code == MAA_WS_LAYOUT_CHANGED) continue; return {}; }
        auto second = provider(error); if (!second) { if (error.code == MAA_WS_LAYOUT_CHANGED) continue; return {}; }
        normalize(*first); normalize(*second);
        const auto a = token(*first), b = token(*second);
        if (a != b) continue;
        for (auto& i : *first) i.layout_token = a;
        error = capture_error(MAA_WS_OK, 0, ""); return MonitorSnapshot { std::move(*first), a };
    }
    error = capture_error(MAA_WS_LAYOUT_CHANGED, MAA_WS_STAGE_ENUMERATE, "Display configuration changed during enumeration"); return {};
}
#if !defined(_WIN32) && !defined(__APPLE__)
std::optional<std::vector<MaaWSMonitorInfo>> platform_monitors(MaaWSError& error) {
    error = capture_error(MAA_WS_UNSUPPORTED_OPTION, MAA_WS_STAGE_ENUMERATE, "Monitor identity snapshots are unsupported on Linux; use an authorized PipeWire or Wlr target"); return {};
}
#endif
bool same_source(const MaaWSFrameInfo& a, const MaaWSFrameInfo& b, MaaWSError& e) {
    if (a.native_id != b.native_id || a.process_id != b.process_id || std::strcmp(a.source_key, b.source_key)) {
        e = capture_error(MAA_WS_TARGET_CHANGED, MAA_WS_STAGE_CAPTURE, "Native source identity changed"); return false;
    }
    if (a.layout_before != b.layout_before || std::memcmp(&a.desktop_rect, &b.desktop_rect, sizeof(a.desktop_rect)) ||
        a.pixel_width != b.pixel_width || a.pixel_height != b.pixel_height || a.scale_x != b.scale_x || a.scale_y != b.scale_y || a.rotation != b.rotation) {
        e = capture_error(MAA_WS_LAYOUT_CHANGED, MAA_WS_STAGE_CAPTURE, "Source layout changed during capture"); return false;
    }
    return true;
}
}
namespace mws {
std::optional<MonitorSnapshot> MonitorSnapshot::create(MaaWSError& e) { return detail::snapshot_from(detail::platform_monitors, e); }
}
