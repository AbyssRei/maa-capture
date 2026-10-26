#pragma once

// ============================================================================
// DXGI Desktop Duplication + 窗口裁剪截图单元（提取自 MaaFramework
// source/MaaWin32ControlUnit/Screencap/DesktopDupWindowScreencap.*)
// ============================================================================

#include "maa-capture/Common.h"
#include "DesktopDupScreencap.h"

MWS_NS_BEGIN

class DesktopDupWindowScreencap : public DesktopDupScreencap
{
public:
    explicit DesktopDupWindowScreencap(CaptureTarget target)
        : DesktopDupScreencap(target, MAA_WS_METHOD_DXGI_DESKTOP_DUP_WINDOW)
    {
    }

    virtual ~DesktopDupWindowScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> capture_image() override;

private:
    RECT get_window_client_rect_screen() const;
    RECT get_output_desktop_coordinates() const;
};

MWS_NS_END