#pragma once

// ============================================================================
// DXGI Desktop Duplication + 窗口裁剪截图单元（提取自 MaaFramework
// source/MaaWin32ControlUnit/Screencap/DesktopDupWindowScreencap.*)
// ============================================================================

#include "Common.h"
#include "DesktopDupScreencap.h"

MWS_NS_BEGIN

class DesktopDupWindowScreencap : public DesktopDupScreencap
{
public:
    explicit DesktopDupWindowScreencap(HWND hwnd)
        : DesktopDupScreencap(hwnd)
    {
    }

    virtual ~DesktopDupWindowScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> screencap() override;

private:
    RECT get_window_client_rect_screen() const;
    RECT get_output_desktop_coordinates() const;
};

MWS_NS_END