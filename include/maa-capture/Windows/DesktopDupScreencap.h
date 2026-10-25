#pragma once

// ============================================================================
// DXGI Desktop Duplication 截图单元（提取自 MaaFramework
// source/MaaWin32ControlUnit/Screencap/DesktopDupScreencap.*)
// 捕获整屏输出：按显示器索引（monitor_index，优先），或目标窗口所在的
// 显示器（hwnd 定位），或主显示器（两者均未指定）。
// ============================================================================

#include "maa-capture/Common.h"
#include "maa-capture/ScreencapBase.h"
#include "SafeDXGI.hpp"

MWS_NS_BEGIN

class DesktopDupScreencap : public ScreencapBase
{
public:
    // monitor_index >= 0 时按显示器索引定位（优先于 hwnd；索引 0 = 主显示器，
    // 其余按 EnumDisplayMonitors 顺序，越界回退主显示器）；
    // -1 时维持原行为：hwnd 定位目标窗口所在显示器，hwnd 为空则主显示器。
    explicit DesktopDupScreencap(HWND hwnd = nullptr, int monitor_index = -1)
        : hwnd_(hwnd)
        , monitor_index_(monitor_index)
    {
    }

    virtual ~DesktopDupScreencap() override;

public: // from ScreencapBase
    virtual std::optional<Image> screencap() override;

protected:
    bool init();
    bool init_texture(ID3D11Texture2D* raw_texture);
    void uninit();
    std::optional<Image> screencap_impl();

    ID3D11Device* d3d_device_ = nullptr;
    ID3D11DeviceContext* d3d_context_ = nullptr;
    IDXGIFactory* dxgi_factory_ = nullptr;
    IDXGIAdapter* dxgi_adapter_ = nullptr;
    IDXGIOutput1* dxgi_output_ = nullptr;
    IDXGIOutputDuplication* dxgi_dup_ = nullptr;

    ID3D11Texture2D* readable_texture_ = nullptr;
    D3D11_TEXTURE2D_DESC texture_desc_ { 0 };

    HWND hwnd_ = nullptr;
    int monitor_index_ = -1; // >= 0：按索引选显示器（优先于 hwnd_）
    HMONITOR current_monitor_ = nullptr;
    bool output_just_initialized_ = false;

private:
    bool init_d3d_device();
    bool init_dxgi_factory();
    bool find_output_by_monitor(HMONITOR monitor);
    bool init_primary_output();
    bool init_output_duplication();
    bool ensure_output_for_monitor();
};

MWS_NS_END