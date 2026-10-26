#pragma once
#include "ScreencapBase.h"
#include <map>
#include <memory>
#include <string>
MWS_NS_BEGIN
using ScreencapMethod = uint64_t;
constexpr auto Method_None = MAA_WS_METHOD_NONE;
constexpr auto Method_GDI = MAA_WS_METHOD_GDI;
constexpr auto Method_FramePool = MAA_WS_METHOD_FRAMEPOOL;
constexpr auto Method_DXGI_DesktopDup = MAA_WS_METHOD_DXGI_DESKTOP_DUP;
constexpr auto Method_DXGI_DesktopDup_Window = MAA_WS_METHOD_DXGI_DESKTOP_DUP_WINDOW;
constexpr auto Method_PrintWindow = MAA_WS_METHOD_PRINTWINDOW;
constexpr auto Method_ScreenDC = MAA_WS_METHOD_SCREENDC;
constexpr auto Method_PipeWire = MAA_WS_METHOD_PIPEWIRE;
constexpr auto Method_Wlr = MAA_WS_METHOD_WLR;
constexpr auto Method_ScreenCaptureKit = MAA_WS_METHOD_SCREENCAPTUREKIT;
ScreencapMethod available_builtin_methods();
const char* method_name(ScreencapMethod method);
class ScreencapMgr {
public:
    explicit ScreencapMgr(CaptureOptions options);
    ~ScreencapMgr();
    ScreencapMgr(const ScreencapMgr&) = delete;
    ScreencapMgr& operator=(const ScreencapMgr&) = delete;
    bool register_unit(std::string key, uint32_t scope, uint32_t valid_fields, std::shared_ptr<ScreencapBase> unit);
    bool connect();
    bool connected() const { return connected_; }
    std::optional<CaptureFrame> screencap();
    const std::string& selected_unit_name() const { return selected_unit_name_; }
    const MaaWSError& last_error() const { return error_; }
    bool fail(MaaWSError error) { error_ = error; return false; }
    void inactive();
private:
    void build_builtin_units();
    bool validate_custom_frame(CaptureFrame& frame, uint32_t stage);
    const CaptureOptions options_;
    std::map<std::string, std::shared_ptr<ScreencapBase>> units_;
    std::shared_ptr<ScreencapBase> selected_;
    std::string selected_unit_name_;
    MaaWSError error_ = capture_error(MAA_WS_OK, 0, "");
    uint64_t next_frame_ = 0;
    uint32_t custom_valid_fields_ = 0;
    bool connected_ = false, active_ = false;
};
MWS_NS_END
