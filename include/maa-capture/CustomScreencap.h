#pragma once
#include "MaaWSCustomScreencap.h"
#include "ScreencapBase.h"
namespace mws {
class CustomScreencapBridge : public ScreencapBase {
public:
    CustomScreencapBridge(std::string key, uint32_t scope, uint32_t valid_fields, const MaaWSCustomScreencapCallbacks& callbacks, void* context)
        : key_(std::move(key)), scope_(scope), valid_fields_(valid_fields), callbacks_(callbacks), context_(context) {}
    std::optional<CaptureFrame> screencap() override;
    void inactive() override;
private:
    std::string key_;
    uint32_t scope_, valid_fields_;
    MaaWSCustomScreencapCallbacks callbacks_;
    void* context_;
    bool active_ = false;
    bool connected_ = false;
};
}
