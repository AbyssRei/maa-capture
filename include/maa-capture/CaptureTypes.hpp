#pragma once
#include "CaptureTypes.h"
#include "Common.h"
#include "Image.hpp"
#include <cstring>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>
MWS_NS_BEGIN
inline bool valid_utf8(std::string_view text) {
    for (size_t n=0;n<text.size();) {
        const auto lead=static_cast<unsigned char>(text[n++]);
        if (lead<0x80) continue;
        unsigned count=0, value=0, minimum=0;
        if (lead>=0xc2 && lead<=0xdf) { count=1;value=lead&31;minimum=0x80; }
        else if (lead>=0xe0 && lead<=0xef) { count=2;value=lead&15;minimum=0x800; }
        else if (lead>=0xf0 && lead<=0xf4) { count=3;value=lead&7;minimum=0x10000; }
        else return false;
        if (count>text.size()-n) return false;
        while(count--) {const auto next=static_cast<unsigned char>(text[n++]);if((next&0xc0)!=0x80)return false;value=(value<<6)|(next&63);}
        if(value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff))return false;
    }
    return true;
}
template <size_t N> inline bool valid_text(const char (&value)[N]) {
    const char* end=static_cast<const char*>(std::memchr(value,0,N));
    return end && valid_utf8(std::string_view(value,end-value));
}
template <size_t N> inline void copy_text(char (&out)[N], std::string_view value) {
    if (value.size() >= N || value.find('\0') != std::string_view::npos || !valid_utf8(value)) throw std::invalid_argument("Invalid UTF-8 string, capacity or NUL");
    std::memset(out, 0, N); std::memcpy(out, value.data(), value.size());
}
inline MaaWSError capture_error(uint32_t code, uint32_t stage, std::string_view message, uint64_t method = 0, int64_t native = 0, std::string_view domain = {}) {
    MaaWSError e {}; e.struct_size = sizeof(e); e.code = code; e.stage = stage; e.method = method; e.native_code = native;
    copy_text(e.message, message.substr(0, sizeof(e.message) - 1)); copy_text(e.native_domain, domain); return e;
}
struct CaptureTarget : MaaWSCaptureTarget {
    CaptureTarget() : MaaWSCaptureTarget{} { struct_size = sizeof(MaaWSCaptureTarget); }
    explicit CaptureTarget(const MaaWSCaptureTarget& value) : MaaWSCaptureTarget(value) {}
    static CaptureTarget monitor(std::string_view key) { CaptureTarget t; t.type = MAA_WS_TARGET_MONITOR; copy_text(t.source_key, key); return t; }
    static CaptureTarget window(uint64_t id, uint32_t platform, uint64_t pid = 0) { CaptureTarget t; t.type = MAA_WS_TARGET_WINDOW; t.platform = platform; t.window_id = id; t.process_id = pid; return t; }
    static CaptureTarget custom(std::string_view key, uint32_t scope = MAA_WS_SCOPE_UNKNOWN) { CaptureTarget t; t.type = MAA_WS_TARGET_CUSTOM; t.scope = scope; copy_text(t.source_key, key); return t; }
    static CaptureTarget pipewire(int32_t fd, uint32_t node, uint32_t scope = MAA_WS_SCOPE_SOURCE) { CaptureTarget t; t.type = MAA_WS_TARGET_PIPEWIRE; t.platform = MAA_WS_PLATFORM_LINUX; t.pipewire_fd = fd; t.node_id = node; t.scope = scope; return t; }
    static CaptureTarget wlr(std::string_view socket, uint32_t output) { CaptureTarget t; t.type = MAA_WS_TARGET_WLR; t.platform = MAA_WS_PLATFORM_LINUX; t.output_index = output; copy_text(t.socket_name, socket); return t; }
};
struct CaptureOptions {
    CaptureTarget target;
    uint64_t methods = 0, policy_flags = 0, max_sample_age_ns = 0;
    uint64_t connect_timeout_ns = 0, capture_timeout_ns = 0, first_frame_timeout_ns = 0;
};
struct CaptureFrame {
    Image image;
    MaaWSFrameInfo source {};
    MaaWSFrameSampleInfo sample {sizeof(MaaWSFrameSampleInfo), MAA_WS_SAMPLE_UNKNOWN, 0, 0, 0, 0};
};
inline bool valid_frame_geometry(const MaaWSFrameInfo& i, uint32_t width, uint32_t height) {
    if ((i.valid_fields & MAA_WS_VALID_SCALE) && (!std::isfinite(i.scale_x) || !std::isfinite(i.scale_y) || i.scale_x<=0 || i.scale_y<=0)) return false;
    if ((i.valid_fields & MAA_WS_VALID_ROTATION) && i.rotation!=0 && i.rotation!=90 && i.rotation!=180 && i.rotation!=270) return false;
    if (i.valid_fields & MAA_WS_VALID_GEOMETRY) {
        const auto& r=i.content_rect; const auto& d=i.desktop_rect;
        if (i.pixel_width!=width || i.pixel_height!=height || i.coordinate_unit<1 || i.coordinate_unit>3 ||
            i.coordinate_space<1 || i.coordinate_space>2 || i.axis!=MAA_WS_AXIS_X_RIGHT_Y_DOWN ||
            !std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) || !std::isfinite(r.height) ||
            !std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.width) || !std::isfinite(d.height) ||
            r.x<0 || r.y<0 || r.width<=0 || r.height<=0 || r.x+r.width>width || r.y+r.height>height || d.width<=0 || d.height<=0) return false;
    }
    if ((i.valid_fields & MAA_WS_VALID_MONITOR) && (i.monitor.struct_size!=sizeof(i.monitor) || !valid_text(i.monitor.monitor_key) || !valid_text(i.monitor.name))) return false;
    return true;
}
struct MonitorSnapshot {
    std::vector<MaaWSMonitorInfo> items;
    uint64_t layout_token = 0;
    static std::optional<MonitorSnapshot> create(MaaWSError& error);
};
uint32_t current_platform();
uint32_t target_scope(const MaaWSCaptureTarget& target);
bool validate_options(const CaptureOptions& options, MaaWSError& error);
bool method_capabilities(uint64_t method, MaaWSMethodCapabilities& info, MaaWSError& error);
bool method_behavior(uint64_t method, MaaWSMethodBehavior& info, MaaWSError& error);
MWS_NS_END
