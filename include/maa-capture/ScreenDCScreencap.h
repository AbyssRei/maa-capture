#pragma once

// ============================================================================
// ScreenDC 截图单元（提取自 MaaFramework source/MaaWin32ControlUnit/Screencap/ScreenDCScreencap.*)
// ============================================================================

#include "Common.h"
#include "SafeWindows.hpp"
#include "ScreencapBase.h"

MWS_NS_BEGIN

class ScreenDCScreencap : public ScreencapBase
{
public:
    explicit ScreenDCScreencap(HWND hwnd)
        : hwnd_(hwnd)
    {
    }

    virtual ~ScreenDCScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> screencap() override;

private:
    HWND hwnd_ = nullptr;
};

MWS_NS_END