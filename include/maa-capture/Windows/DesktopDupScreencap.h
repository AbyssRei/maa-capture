#pragma once

// Strict DXGI output capture. No index or primary-output fallback.

#include "maa-capture/Common.h"
#include "maa-capture/ScreencapBase.h"
#include "SafeDXGI.hpp"

MWS_NS_BEGIN

class DesktopDupScreencap : public TargetScreencapBase
{
public:
    explicit DesktopDupScreencap(CaptureTarget target, uint64_t method = MAA_WS_METHOD_DXGI_DESKTOP_DUP, bool cache_timeout = false)
        : TargetScreencapBase(target, method, cache_timeout)
        , hwnd_(reinterpret_cast<HWND>(static_cast<uintptr_t>(target.window_id)))
    {}
    virtual ~DesktopDupScreencap() override;
    void inactive() override { uninit(); }

public: // from ScreencapBase
    virtual std::optional<Image> capture_image() override;

protected:
    bool init();
    bool init_texture(ID3D11Texture2D* raw_texture);
    void uninit();
    std::optional<Image> screencap_impl();

    ID3D11Device* d3d_device_ = nullptr;
    ID3D11DeviceContext* d3d_context_ = nullptr;
    IDXGIFactory1* dxgi_factory_ = nullptr;
    IDXGIAdapter* dxgi_adapter_ = nullptr;
    IDXGIOutput1* dxgi_output_ = nullptr;
    IDXGIOutputDuplication* dxgi_dup_ = nullptr;

    ID3D11Texture2D* readable_texture_ = nullptr;
    D3D11_TEXTURE2D_DESC texture_desc_ { 0 };

    HWND hwnd_ = nullptr;
    uint64_t output_layout_ = 0;
    HMONITOR current_monitor_ = nullptr;
    bool output_just_initialized_ = false;

private:
    bool init_d3d_device();
    bool init_dxgi_factory();
    bool find_output_by_monitor(HMONITOR monitor);
    bool init_output_duplication();
    bool ensure_output_for_monitor();
};

MWS_NS_END
