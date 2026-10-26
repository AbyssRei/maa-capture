#pragma once

// ============================================================================
// ScreenDC 截图单元（提取自 MaaFramework source/MaaWin32ControlUnit/Screencap/ScreenDCScreencap.*)
// ============================================================================

#include "maa-capture/Common.h"
#include "SafeWindows.hpp"
#include "maa-capture/ScreencapBase.h"

MWS_NS_BEGIN

class ScreenDCScreencap : public TargetScreencapBase
{
public:
    explicit ScreenDCScreencap(CaptureTarget target)
        : TargetScreencapBase(target, MAA_WS_METHOD_SCREENDC)
        , hwnd_(reinterpret_cast<HWND>(static_cast<uintptr_t>(target.window_id)))
    {
    }

    virtual ~ScreenDCScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> capture_image() override;

private:
    HWND hwnd_ = nullptr;
};

MWS_NS_END