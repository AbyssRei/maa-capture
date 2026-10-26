#pragma once
#include <optional>
#include "CaptureTypes.hpp"
MWS_NS_BEGIN
class ScreencapBase {
public:
    virtual ~ScreencapBase() = default;
    virtual std::optional<CaptureFrame> screencap() = 0;
    virtual void inactive() {}
    virtual const MaaWSError& last_error() const { return error_; }
protected:
    MaaWSError error_ = capture_error(MAA_WS_OK, 0, "");
};
/* Shared native source transaction; pixel helpers copy while native buffers
 * are valid. Publication requires before/after verification of this source. */
class TargetScreencapBase : public ScreencapBase {
public:
    TargetScreencapBase(CaptureTarget target, uint64_t method) : target_(std::move(target)), method_(method) {}
    std::optional<CaptureFrame> screencap() override;
    bool validate_source();
protected:
    virtual std::optional<Image> capture_image() = 0;
    const CaptureTarget target_;
    const uint64_t method_;
    MaaWSFrameInfo source_ {};
    uint64_t process_identity_ = 0;
};
MWS_NS_END
