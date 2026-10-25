#pragma once

// ============================================================================
// PrintWindow 截图单元（提取自 MaaFramework
// source/MaaWin32ControlUnit/Screencap/PrintWindowScreencap.*)
// 支持后台截图；对活动弹窗（对话框等）做原位合成叠加。
// ============================================================================

#include "maa-capture/Common.h"
#include "SafeWindows.hpp"
#include "maa-capture/ScreencapBase.h"

MWS_NS_BEGIN

class PrintWindowScreencap : public ScreencapBase
{
public:
    explicit PrintWindowScreencap(HWND hwnd)
        : hwnd_(hwnd)
    {
    }

    virtual ~PrintWindowScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> screencap() override;

private:
    std::optional<Image> capture_window(HWND capture_hwnd);

    HWND hwnd_ = nullptr;
};

MWS_NS_END