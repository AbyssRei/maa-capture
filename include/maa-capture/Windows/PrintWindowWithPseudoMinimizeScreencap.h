#pragma once

// ============================================================================
// PrintWindow + 伪最小化包装（提取自 MaaFramework
// source/MaaWin32ControlUnit/Screencap/PrintWindowWithPseudoMinimizeScreencap.h）
// ============================================================================

#include "maa-capture/Common.h"
#include "PrintWindowScreencap.h"
#include "PseudoMinimizeHelper.h"

MWS_NS_BEGIN

class PrintWindowWithPseudoMinimizeScreencap : public ScreencapBase
{
public:
    explicit PrintWindowWithPseudoMinimizeScreencap(CaptureTarget target)
        : inner_(target)
        , helper_(reinterpret_cast<HWND>(static_cast<uintptr_t>(target.window_id)))
    {
    }

    virtual ~PrintWindowWithPseudoMinimizeScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<CaptureFrame> screencap() override
    {
        if (operation_ && !operation_->check(error_,MAA_WS_METHOD_PRINTWINDOW)) return {};
        if (!inner_.validate_source()) { error_ = inner_.last_error(); return {}; }
        if (!helper_.start() || !helper_.ensure_not_minimized()) {
            error_ = helper_.last_error(); error_.method = MAA_WS_METHOD_PRINTWINDOW; return {};
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
        inner_.inactive();
        // Stop monitoring and restore both window attributes and minimization intent.
        if (!helper_.stop()) { error_ = helper_.last_error(); error_.method = MAA_WS_METHOD_PRINTWINDOW; return false; }
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
    PrintWindowScreencap inner_;
    PseudoMinimizeHelper helper_;
};

MWS_NS_END
