#pragma once

// ============================================================================
// ScreenCaptureKit 截图单元（macOS）
//
// 提取自 MaaFramework source/MaaMacOSControlUnit/Screencap/ScreenCaptureKitScreencap.h/.mm
// （cv::Mat → mws::Image，日志宏替换为独立库 Logger）。
//
// 需要 macOS 14.0+（SCScreenshotManager）；低版本上 screencap() 返回失败。
// window_id 为 0 时截取整个主显示器，否则按 CGWindowID 截取指定窗口。
// 运行时需要屏幕录制权限（TCC）。
// ============================================================================

// 注意：编译进库需要 macOS 14+ SDK（CMake 自动链接 ScreenCaptureKit）；
// 本头文件本身只要求 Apple 平台。

#if !defined(__APPLE__)
#error "ScreenCaptureKitScreencap is macOS-only"
#endif

#include <cstdint>
#include <optional>

#include "maa-capture/Common.h"
#include "maa-capture/ScreencapBase.h"

MWS_NS_BEGIN

class ScreenCaptureKitScreencap : public ScreencapBase
{
public:
    explicit ScreenCaptureKitScreencap(uint32_t window_id)
        : window_id_(window_id)
    {
    }

    virtual ~ScreenCaptureKitScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> screencap() override;

private:
    std::optional<Image> screencap_window(uint32_t wid);
    std::optional<Image> screencap_display();

    uint32_t window_id_ = 0;
};

MWS_NS_END
