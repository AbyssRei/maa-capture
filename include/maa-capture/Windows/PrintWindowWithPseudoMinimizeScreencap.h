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
    explicit PrintWindowWithPseudoMinimizeScreencap(HWND hwnd)
        : inner_(hwnd)
        , helper_(hwnd)
    {
    }

    virtual ~PrintWindowWithPseudoMinimizeScreencap() override = default;

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
        helper_.stop();
    }

private:
    PrintWindowScreencap inner_;
    PseudoMinimizeHelper helper_;
};

MWS_NS_END