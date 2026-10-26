#pragma once

#include <sdkddkver.h>
#include <atomic>

// ============================================================================
// FramePool (Windows.Graphics.Capture) 截图单元（提取自 MaaFramework
// source/MaaWin32ControlUnit/Screencap/FramePoolScreencap.*)
//
// 需要 Windows 10 SDK 10.0.22000.0 及以上 + C++/WinRT。
// 低于该 SDK 或显式禁用时编译为占位实现（screencap 恒失败）。
// 也可在 CMake 中通过 MWS_ENABLE_FRAMEPOOL=OFF 关闭。
// ============================================================================

#ifndef MWS_FRAMEPOOL_AVAILABLE
#if WDK_NTDDI_VERSION >= 0x0A00000B // NTDDI_WIN10_CO
#define MWS_FRAMEPOOL_AVAILABLE 1
#else
#define MWS_FRAMEPOOL_AVAILABLE 0
#pragma message("Win32 FramePool needs Windows 10 SDK 10.0.22000.0 or higher")
#endif
#endif

#include "maa-capture/Common.h"
#include "maa-capture/ScreencapBase.h"
#include "SafeWindows.hpp"

#if MWS_FRAMEPOOL_AVAILABLE

#include "SafeDXGI.hpp"

#include <winrt/Windows.Graphics.Capture.h>

MWS_NS_BEGIN

class FramePoolScreencap : public TargetScreencapBase
{
public:
    explicit FramePoolScreencap(CaptureTarget target, bool cache_timeout = false);

    virtual ~FramePoolScreencap() override;

public: // from ScreencapBase
    std::optional<CaptureFrame> screencap() override;
    bool validate_source() override;
    const MaaWSError& last_error() const override { return foreground_error_; }
    virtual std::optional<Image> capture_image() override;
    void inactive() override;
    bool validate_pending_frame(const CaptureFrame&) override;

private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
    std::optional<Image> capture_on_worker();
    bool init();
    bool init_texture(winrt::com_ptr<ID3D11Texture2D> raw_texture);
    void uninit();
    bool check_and_handle_size_changed();
    void try_disable_border();
    void try_disable_cursor();
    void try_include_secondary_windows();

private:
    HWND hwnd_ = nullptr;

    winrt::com_ptr<ID3D11Device> d3d_device_ = nullptr;
    winrt::com_ptr<ID3D11DeviceContext> d3d_context_ = nullptr;
    winrt::com_ptr<ID3D11Texture2D> readable_texture_ = nullptr;
    D3D11_TEXTURE2D_DESC texture_desc_ = { };

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem cap_item_ = nullptr;
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool cap_frame_pool_ = nullptr;
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession cap_session_ = nullptr;

    // 存储上次的窗口大小，用于检测窗口大小变化
    std::pair<int, int> last_capture_size_ = { 0, 0 };
    std::shared_ptr<std::atomic_bool> source_closed_;
    winrt::event_token closed_token_ {};
    std::shared_ptr<std::atomic_bool> frame_ready_;
    winrt::event_token arrived_token_ {};
    bool refreshing_cache_ = false;
    int64_t native_sample_time_ = 0;
    RECT captured_bounds_ {};
    MaaWSError foreground_error_ = capture_error(MAA_WS_OK,0,"");
};

MWS_NS_END

#else // !MWS_FRAMEPOOL_AVAILABLE

MWS_NS_BEGIN

class FramePoolScreencap : public TargetScreencapBase
{
public:
    explicit FramePoolScreencap(CaptureTarget target, bool cache_timeout = false)
        : TargetScreencapBase(target, MAA_WS_METHOD_FRAMEPOOL, cache_timeout)
    {
    }

    virtual ~FramePoolScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> capture_image() override
    {
        return std::nullopt;
    }
};

MWS_NS_END

#endif
