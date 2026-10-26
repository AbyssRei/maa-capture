#pragma once
#include "maa-capture/ScreencapBase.h"
namespace mws {
class ScreenCaptureKitScreencap : public TargetScreencapBase {
public:
    explicit ScreenCaptureKitScreencap(CaptureTarget target) : TargetScreencapBase(std::move(target), MAA_WS_METHOD_SCREENCAPTUREKIT) {}
protected:
    std::optional<Image> capture_image() override;
};
}