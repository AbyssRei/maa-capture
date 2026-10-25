#pragma once

// ============================================================================
// GDI 截图单元（提取自 MaaFramework source/MaaWin32ControlUnit/Screencap/GdiScreencap.*）
// ============================================================================

#include "maa-capture/Common.h"
#include "SafeWindows.hpp"
#include "maa-capture/ScreencapBase.h"

MWS_NS_BEGIN

class GdiScreencap : public ScreencapBase
{
public:
    explicit GdiScreencap(HWND hwnd)
        : hwnd_(hwnd)
    {
    }

    virtual ~GdiScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> screencap() override;

private:
    HWND hwnd_ = nullptr;
};

MWS_NS_END