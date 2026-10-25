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
    explicit FramePoolWithPseudoMinimizeScreencap(HWND hwnd)
        : inner_(hwnd)
        , helper_(hwnd)
    {
    }

    virtual ~FramePoolWithPseudoMinimizeScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> screencap() override
    {
        helper_.start();
        helper_.ensure_not_minimized();
        return inner_.screencap();
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