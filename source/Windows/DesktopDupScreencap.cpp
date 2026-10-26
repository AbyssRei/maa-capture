#include "maa-capture/Windows/DesktopDupScreencap.h"

#include "maa-capture/Windows/HwndUtils.hpp"
#include "maa-capture/Logger.h"
#include <chrono>

MWS_NS_BEGIN

DesktopDupScreencap::~DesktopDupScreencap()
{
    uninit();
}

std::optional<Image> DesktopDupScreencap::capture_image()
{
    // 初始化 D3D 设备和 DXGI 工厂（只需要初始化一次）
    if (!dxgi_factory_) {
        if (!init()) {
            LogError << "failed to init_d3d_device";
            uninit();
            return std::nullopt;
        }
    }

    // 确保输出匹配当前窗口所在的显示器（每次截图时检查，支持窗口移动）
    if (!ensure_output_for_monitor()) {
        LogError << "failed to ensure_output_for_monitor";
        return std::nullopt;
    }

    // 如果输出刚初始化，前几张图片可能是空的，跳过
    if (output_just_initialized_) {
        for (int i = 0; i < 3; ++i) {
            auto opt = screencap_impl();
            if (opt) {
                const auto& br = *(opt->end<Pixel4>() - 1);
                if (br[3] == 255) { // only check alpha
                    output_just_initialized_ = false;
                    return bgra_to_bgr(*opt); // retain the only valid static first sample
                }
            }
            if (operation_ && !operation_->check(error_,method_)) return {};
            LogWarn << "blank image, continue";
        }
    }

    auto img = screencap_impl();
    if (!img) {
        return std::nullopt;
    }
    return bgra_to_bgr(*img);
}

bool DesktopDupScreencap::init()
{
    return init_dxgi_factory();
}

bool DesktopDupScreencap::ensure_output_for_monitor()
{
    HMONITOR target_monitor = target_.type == MAA_WS_TARGET_MONITOR
        ? reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(source_.native_id))
        : reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(source_.monitor.native_id));
    if (!target_monitor) {
        error_ = capture_error(MAA_WS_TARGET_NOT_FOUND, MAA_WS_STAGE_CAPTURE, "Target has no DXGI monitor", method_);
        return false;
    }
    if (target_monitor == current_monitor_ && dxgi_dup_ && output_layout_ == source_.layout_before) return true;
    uninit();
    if (!init_dxgi_factory() || !find_output_by_monitor(target_monitor)) {
        error_ = capture_error(MAA_WS_TARGET_NOT_FOUND, MAA_WS_STAGE_CAPTURE, "Requested DXGI output is absent", method_);
        return false;
    }
    DXGI_OUTPUT_DESC desc {};
    const auto hr = dxgi_output_->GetDesc(&desc);
    if (FAILED(hr) || desc.Monitor != target_monitor || !desc.AttachedToDesktop) {
        error_ = capture_error(MAA_WS_SOURCE_MISMATCH, MAA_WS_STAGE_CAPTURE, "DXGI output differs from resolved target", method_, hr, "HRESULT");
        return false;
    }
    if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
        error_ = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CAPTURE, "Rotated DXGI output is not implemented", method_);
        return false;
    }
    auto r = desc.DesktopCoordinates;
    if (target_.type == MAA_WS_TARGET_MONITOR) {
        if (source_.desktop_rect.x != r.left || source_.desktop_rect.y != r.top ||
            source_.pixel_width != uint32_t(r.right-r.left) || source_.pixel_height != uint32_t(r.bottom-r.top)) {
            error_ = capture_error(MAA_WS_LAYOUT_CHANGED, MAA_WS_STAGE_CAPTURE, "DXGI and display configuration disagree", method_); return false;
        }
    } else {
        const auto& w = source_.desktop_rect;
        if (w.x < r.left || w.y < r.top || w.x+w.width > r.right || w.y+w.height > r.bottom) {
            error_ = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CAPTURE, "Window spans outputs or extends beyond the selected output", method_); return false;
        }
    }
    if (!init_d3d_device() || !init_output_duplication()) return false;
    current_monitor_ = target_monitor;
    output_layout_ = source_.layout_before;
    output_just_initialized_ = true;
    return true;
}

bool DesktopDupScreencap::init_d3d_device()
{
    HRESULT ret = D3D11CreateDevice(
        dxgi_adapter_,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &d3d_device_,
        nullptr,
        &d3d_context_);
    if (FAILED(ret)) {
        LogError << "D3D11CreateDevice failed" << VAR(ret);
        return false;
    }
    return true;
}

bool DesktopDupScreencap::init_dxgi_factory()
{
    HRESULT ret = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgi_factory_));
    if (FAILED(ret)) {
        LogError << "CreateDXGIFactory failed" << VAR(ret);
        return false;
    }
    return true;
}

bool DesktopDupScreencap::find_output_by_monitor(HMONITOR monitor)
{
    HRESULT ret = S_OK;

    // 遍历所有适配器，找到包含目标显示器的输出
    for (UINT adapter_index = 0;; ++adapter_index) {
        IDXGIAdapter* adapter = nullptr;
        ret = dxgi_factory_->EnumAdapters(adapter_index, &adapter);
        if (FAILED(ret)) {
            // 没有更多适配器了
            break;
        }

        // 使用 OnScopeLeave 确保 adapter 在作用域结束时被释放（除非被赋值给成员变量）
        bool adapter_used = false;
        OnScopeLeave([&]() {
            if (!adapter_used && adapter) {
                adapter->Release();
            }
        });

        // 遍历该适配器的所有输出
        for (UINT output_index = 0;; ++output_index) {
            IDXGIOutput* output = nullptr;
            ret = adapter->EnumOutputs(output_index, &output);
            if (FAILED(ret)) {
                // 没有更多输出了
                break;
            }

            // 使用 OnScopeLeave 确保 output 在作用域结束时被释放（除非被赋值给成员变量）
            bool output_used = false;
            OnScopeLeave([&]() {
                if (!output_used && output) {
                    output->Release();
                }
            });

            // 获取输出的描述信息
            DXGI_OUTPUT_DESC output_desc;
            ret = output->GetDesc(&output_desc);
            if (SUCCEEDED(ret) && output_desc.Monitor == monitor) {
                // 找到匹配的显示器，通过 QueryInterface 获取 IDXGIOutput1
                IDXGIOutput1* output1 = nullptr;
                ret = output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
                if (FAILED(ret) || !output1) {
                    LogError << "QueryInterface IDXGIOutput1 failed" << VAR(ret);
                    return false;
                }
                dxgi_adapter_ = adapter;
                dxgi_output_ = output1;
                adapter_used = true;
                // output 的引用由 QueryInterface 返回的 output1 持有，释放原始 output
                return true;
            }
        }
    }

    return false;
}

bool DesktopDupScreencap::init_output_duplication()
{
    HRESULT ret = dxgi_output_->DuplicateOutput(d3d_device_, &dxgi_dup_);
    if (FAILED(ret)) {
        error_ = capture_error(ret == E_ACCESSDENIED ? MAA_WS_PERMISSION_DENIED : MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CAPTURE, "DuplicateOutput failed", method_, ret, "HRESULT");
        LogError << "DuplicateOutput failed" << VAR(ret);
        return false;
    }
    return true;
}

bool DesktopDupScreencap::init_texture(ID3D11Texture2D* raw_texture)
{
    LogFunc;

    if (!d3d_device_ || !raw_texture) {
        LogError << "handle is null" << VAR_VOIDP(d3d_device_) << VAR_VOIDP(raw_texture);
        return false;
    }

    raw_texture->GetDesc(&texture_desc_); // basic info

    texture_desc_.BindFlags = 0;
    texture_desc_.MiscFlags = 0;
    texture_desc_.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    texture_desc_.Usage = D3D11_USAGE_STAGING;

    HRESULT ret = d3d_device_->CreateTexture2D(&texture_desc_, NULL, &readable_texture_);
    if (FAILED(ret)) {
        LogError << "CreateTexture2D failed" << VAR(ret);
        return false;
    }

    return true;
}

void DesktopDupScreencap::uninit()
{
    LogFunc;
    invalidate_session();
    current_monitor_ = nullptr; output_layout_ = 0; output_just_initialized_ = false;

    if (readable_texture_) {
        readable_texture_->Release();
        readable_texture_ = nullptr;
    }
    texture_desc_ = { 0 };

    if (dxgi_dup_) {
        dxgi_dup_->Release();
        dxgi_dup_ = nullptr;
    }
    if (dxgi_output_) {
        dxgi_output_->Release();
        dxgi_output_ = nullptr;
    }
    if (dxgi_adapter_) {
        dxgi_adapter_->Release();
        dxgi_adapter_ = nullptr;
    }
    if (dxgi_factory_) {
        dxgi_factory_->Release();
        dxgi_factory_ = nullptr;
    }
    if (d3d_context_) {
        d3d_context_->Release();
        d3d_context_ = nullptr;
    }
    if (d3d_device_) {
        d3d_device_->Release();
        d3d_device_ = nullptr;
    }
}

std::optional<Image> DesktopDupScreencap::screencap_impl()
{
    if (!d3d_context_ || !dxgi_dup_) {
        LogError << "handle is null" << VAR_VOIDP(d3d_context_) << VAR_VOIDP(dxgi_dup_);
        return std::nullopt;
    }

    HRESULT ret = S_OK;

    DXGI_OUTDUPL_FRAME_INFO frame_info { 0 };
    IDXGIResource* desktop_resource = nullptr;
    const auto wait_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    do {
        if (operation_ && !operation_->check(error_,method_)) return {};
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(wait_until - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) { ret = DXGI_ERROR_WAIT_TIMEOUT; break; }
        const auto poll = std::min<uint32_t>(uint32_t(std::min<int64_t>(remaining,20)), operation_ ? operation_->poll_ms() : 20);
        ret = dxgi_dup_->AcquireNextFrame(poll,&frame_info,&desktop_resource);
    } while (ret == DXGI_ERROR_WAIT_TIMEOUT);
    if (operation_ && !operation_->check(error_,method_)) {
        if (SUCCEEDED(ret)) { dxgi_dup_->ReleaseFrame(); if (desktop_resource) desktop_resource->Release(); }
        return {};
    }
    if (ret == DXGI_ERROR_ACCESS_LOST) {
        LogWarn << "Desktop duplication access lost, reinitializing";
        error_ = capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CAPTURE, "Desktop duplication access lost", method_,ret,"HRESULT");
        uninit();
        return std::nullopt;
    }
    if (FAILED(ret)) {
        if (ret == DXGI_ERROR_WAIT_TIMEOUT && FAILED(d3d_device_->GetDeviceRemovedReason())) {
            error_ = capture_error(MAA_WS_CAPTURE_FAILED,MAA_WS_STAGE_CAPTURE,"DXGI device was lost",method_,d3d_device_->GetDeviceRemovedReason(),"HRESULT");
            uninit(); return {};
        }
        error_ = capture_error(ret == DXGI_ERROR_WAIT_TIMEOUT ? MAA_WS_TIMEOUT : MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CAPTURE, "AcquireNextFrame failed", method_, ret, "HRESULT");
        LogError << "AcquireNextFrame failed" << VAR(ret);
        return std::nullopt;
    }
    OnScopeLeave([&]() {
        dxgi_dup_->ReleaseFrame();

        if (desktop_resource) {
            desktop_resource->Release();
            desktop_resource = nullptr;
        }
    });

    ID3D11Texture2D* raw_texture = nullptr;
    ret = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&raw_texture));
    if (FAILED(ret)) {
        LogError << "QueryInterface ID3D11Texture2D failed" << VAR(ret);
        return std::nullopt;
    }
    OnScopeLeave([&]() {
        if (raw_texture) {
            raw_texture->Release();
            raw_texture = nullptr;
        }
    });

    if (!readable_texture_ && !init_texture(raw_texture)) {
        LogError << "failed to init_texture";
        return std::nullopt;
    }

    d3d_context_->CopyResource(readable_texture_, raw_texture);

    D3D11_MAPPED_SUBRESOURCE mapped { 0 };
    ret = d3d_context_->Map(readable_texture_, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(ret)) {
        LogError << "Map failed" << VAR(ret);
        error_ = capture_error(MAA_WS_CAPTURE_FAILED,MAA_WS_STAGE_CAPTURE,"DXGI texture map failed",method_,ret,"HRESULT");
        return std::nullopt;
    }
    OnScopeLeave([&]() { d3d_context_->Unmap(readable_texture_, 0); });

    Image mat = Image::wrap(texture_desc_.Height, texture_desc_.Width, 4, mapped.pData, mapped.RowPitch);
    return mat.clone();
}

MWS_NS_END
