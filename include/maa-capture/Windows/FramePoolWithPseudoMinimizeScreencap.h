#pragma once

// ============================================================================
// FramePool + 伪最小化包装（提取自 MaaFramework
// source/MaaWin32ControlUnit/Screencap/FramePoolWithPseudoMinimizeScreencap.h）
// ============================================================================

#include "maa-capture/Common.h"
#include "FramePoolScreencap.h"
#include "PseudoMinimizeHelper.h"

MWS_NS_BEGIN

class FramePoolWithPseudoMinimizeScreencap : public ScreencapBase
{
public:
    explicit FramePoolWithPseudoMinimizeScreencap(CaptureTarget target, bool cache_timeout = false)
        : inner_(target, cache_timeout)
        , helper_(reinterpret_cast<HWND>(static_cast<uintptr_t>(target.window_id)))
    {
    }

    virtual ~FramePoolWithPseudoMinimizeScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<CaptureFrame> screencap() override
    {
        if (operation_ && !operation_->check(error_,MAA_WS_METHOD_FRAMEPOOL)) return {};
        if (!inner_.validate_source()) { error_ = inner_.last_error(); return {}; }
        if (!helper_.start() || !helper_.ensure_not_minimized()) {
            error_ = helper_.last_error(); error_.method = MAA_WS_METHOD_FRAMEPOOL; return {};
        }
        auto frame = inner_.screencap();
        error_ = inner_.last_error();
        return frame;
    }

    virtual void inactive() override
    {
        release_resources();
    }
    bool release_resources() override
    {
        // Stop monitoring and restore both window attributes and minimization intent.
        try { inner_.inactive(); } catch (...) { helper_.stop(); throw; }
        if (!helper_.stop()) { error_ = helper_.last_error(); error_.method = MAA_WS_METHOD_FRAMEPOOL; return false; }
        return true;
    }

    bool validate_pending_frame(const CaptureFrame& frame) override {
        const bool valid = inner_.validate_pending_frame(frame);
        if (!valid) error_ = inner_.last_error();
        return valid;
    }
    void set_operation(std::shared_ptr<CaptureOperation> operation) override {
        if (operation && (operation->policy_flags & MAA_WS_POLICY_STRICT_BACKGROUND)) helper_.require_no_activate();
        ScreencapBase::set_operation(operation); inner_.set_operation(std::move(operation));
    }
    void enable_diagnostics(bool enabled) override { ScreencapBase::enable_diagnostics(enabled); inner_.enable_diagnostics(enabled); }

private:
    FramePoolScreencap inner_;
    PseudoMinimizeHelper helper_;
};

MWS_NS_END
