#pragma once

#include <sdkddkver.h>

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

#include "Common.h"
#include "ScreencapBase.h"

#if MWS_FRAMEPOOL_AVAILABLE

#include "SafeDXGI.hpp"
#include "SafeWindows.hpp"

#include <winrt/Windows.Graphics.Capture.h>

MWS_NS_BEGIN

class FramePoolScreencap : public ScreencapBase
{
public:
    explicit FramePoolScreencap(HWND hwnd)
        : hwnd_(hwnd)
    {
    }

    virtual ~FramePoolScreencap() override;

public: // from ScreencapBase
    virtual std::optional<Image> screencap() override;

private:
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
    winrt::com_ptr<IDXGISwapChain> dxgi_swap_chain_ = nullptr;
    winrt::com_ptr<ID3D11Texture2D> readable_texture_ = nullptr;
    D3D11_TEXTURE2D_DESC texture_desc_ = { };

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem cap_item_ = nullptr;
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool cap_frame_pool_ = nullptr;
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession cap_session_ = nullptr;

    // 存储上次的窗口大小，用于检测窗口大小变化
    std::pair<int, int> last_capture_size_ = { 0, 0 };
    Image cached_image_;
};

MWS_NS_END

#else // !MWS_FRAMEPOOL_AVAILABLE

MWS_NS_BEGIN

class FramePoolScreencap : public ScreencapBase
{
public:
    explicit FramePoolScreencap(HWND)
    {
    }

    virtual ~FramePoolScreencap() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> screencap() override
    {
        return std::nullopt;
    }
};

MWS_NS_END

#endif