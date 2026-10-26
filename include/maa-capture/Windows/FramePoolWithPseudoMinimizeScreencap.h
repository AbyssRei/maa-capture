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
    explicit FramePoolWithPseudoMinimizeScreencap(CaptureTarget target)
        : inner_(target)
        , helper_(reinterpret_cast<HWND>(static_cast<uintptr_t>(target.window_id)))
    {
    }

    virtual ~FramePoolWithPseudoMinimizeScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<CaptureFrame> screencap() override
    {
        if (!inner_.validate_source()) { error_ = inner_.last_error(); return {}; }
        helper_.start();
        helper_.ensure_not_minimized();
        auto frame = inner_.screencap();
        error_ = inner_.last_error();
        return frame;
    }

    virtual void inactive() override
    {
        // Stop monitoring and restore both window attributes and minimization intent.
        inner_.inactive();
        helper_.stop();
    }

private:
    FramePoolScreencap inner_;
    PseudoMinimizeHelper helper_;
};

MWS_NS_END
