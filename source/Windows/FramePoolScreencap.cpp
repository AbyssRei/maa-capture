#include "maa-capture/Windows/FramePoolScreencap.h"

#if MWS_FRAMEPOOL_AVAILABLE

#include <dwmapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

#include <thread>
#include <condition_variable>
#include <future>
#include <mutex>
#include <roapi.h>

#include "maa-capture/Windows/HwndUtils.hpp"
#include "maa-capture/Logger.h"
#include "maa-capture/Image.hpp"

MWS_NS_BEGIN
namespace {
MaaWSError framepool_error(const char* message, HRESULT code) {
    return capture_error(code == E_ACCESSDENIED ? MAA_WS_PERMISSION_DENIED : MAA_WS_CAPTURE_FAILED,
        MAA_WS_STAGE_CAPTURE, message, MAA_WS_METHOD_FRAMEPOOL, code, "HRESULT");
}
struct RefreshScope {
    bool& flag;
    explicit RefreshScope(bool& value) : flag(value) { flag = true; }
    ~RefreshScope() { flag = false; }
};
}

struct FramePoolScreencap::Worker {
    using Task = std::packaged_task<std::optional<Image>()>;
    std::mutex mutex;
    std::condition_variable ready;
    std::optional<Task> pending;
    std::jthread thread;
    explicit Worker(FramePoolScreencap* owner) {
        std::promise<void> initialized;
        auto started = initialized.get_future();
        thread = std::jthread([this, owner, init = std::move(initialized)](std::stop_token stop) mutable {
            const auto hr = RoInitialize(RO_INIT_MULTITHREADED);
            if (FAILED(hr)) { init.set_exception(std::make_exception_ptr(winrt::hresult_error(hr))); return; }
            auto old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            init.set_value();
            for (;;) {
                std::unique_lock lock(mutex);
                const auto requested = [&] { return stop.stop_requested() || pending.has_value(); };
                if (owner->cache_timeout_) ready.wait_for(lock, std::chrono::milliseconds(4), requested);
                else ready.wait(lock, requested);
                if (stop.stop_requested() && !pending) break;
                if (!pending) {
                    lock.unlock();
                    // Only this backend's worker touches WinRT resources. Use
                    // try_lock: a host capture may hold capture_mutex_ while
                    // waiting for its queued worker operation.
                    std::unique_lock capture_lock(owner->capture_mutex_, std::try_to_lock);
                    if (capture_lock && owner->cache_timeout_ && owner->cached_frame_ && owner->frame_ready_ && owner->frame_ready_->exchange(false)) {
                        RefreshScope refresh(owner->refreshing_cache_);
                        try { owner->TargetScreencapBase::screencap(); }
                        catch (...) { try { owner->uninit(); } catch (...) {} }
                    }
                    continue;
                }
                Task task = std::move(*pending); pending.reset(); lock.unlock();
                task(); // packaged_task transfers exceptions to the caller
            }
            owner->uninit();
            winrt::clear_factory_cache();
            if (old) SetThreadDpiAwarenessContext(old);
            RoUninitialize();
        });
        started.get();
    }
    ~Worker() { thread.request_stop(); ready.notify_one(); }
    std::optional<Image> invoke(std::function<std::optional<Image>()> operation) {
        Task task(std::move(operation)); auto result = task.get_future();
        { std::lock_guard lock(mutex); pending.emplace(std::move(task)); }
        ready.notify_one(); return result.get();
    }
};
FramePoolScreencap::FramePoolScreencap(CaptureTarget target, bool cache_timeout)
    : TargetScreencapBase(target, MAA_WS_METHOD_FRAMEPOOL, cache_timeout)
    , hwnd_(reinterpret_cast<HWND>(static_cast<uintptr_t>(target.window_id))) {}
FramePoolScreencap::~FramePoolScreencap() { worker_.reset(); }
std::optional<CaptureFrame> FramePoolScreencap::screencap() {
    std::lock_guard lock(capture_mutex_);
    auto frame = TargetScreencapBase::screencap();
    foreground_error_ = error_; return frame;
}
bool FramePoolScreencap::validate_source() {
    std::lock_guard lock(capture_mutex_);
    const bool valid = TargetScreencapBase::validate_source();
    foreground_error_ = error_; return valid;
}
bool FramePoolScreencap::validate_pending_frame(const CaptureFrame& frame) {
    std::lock_guard lock(capture_mutex_);
    struct Report {FramePoolScreencap& owner;~Report(){if(!owner.refreshing_cache_)owner.foreground_error_=owner.error_;}} report{*this};
    if (!TargetScreencapBase::validate_pending_frame(frame)) return false;
    if (source_closed_ && source_closed_->load()) {
        error_ = capture_error(MAA_WS_TARGET_CHANGED, MAA_WS_STAGE_CAPTURE, "FramePool item closed before publication", method_); return false;
    }
    RECT bounds {};
    if (FAILED(DwmGetWindowAttribute(hwnd_, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) && !GetWindowRect(hwnd_, &bounds)) {
        error_ = capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CAPTURE, "Cannot validate FramePool crop", method_, GetLastError(), "Win32"); return false;
    }
    if (!EqualRect(&bounds, &captured_bounds_)) {
        error_ = capture_error(MAA_WS_LAYOUT_CHANGED, MAA_WS_STAGE_CAPTURE, "FramePool crop changed before publication", method_); return false;
    }
    return true;
}
void FramePoolScreencap::inactive() {
    std::lock_guard lock(capture_mutex_);
    if (worker_) worker_->invoke([this]() -> std::optional<Image> { uninit(); return {}; });
    worker_.reset();
    invalidate_session();
}
std::optional<Image> FramePoolScreencap::capture_image() {
    try {
        if (refreshing_cache_) return capture_on_worker();
        if (!worker_) worker_ = std::make_unique<Worker>(this);
        return worker_->invoke([this] {
            // Catch an arrival that raced with the host's request before it
            // drains the old queue. It belongs to this already validated
            // session and passes the same source/size publication checks.
            if (cache_timeout_ && cached_frame_ && frame_ready_ && frame_ready_->exchange(false)) {
                RefreshScope refresh(refreshing_cache_);
                auto queued = capture_on_worker();
                if (queued || error_.code != MAA_WS_TIMEOUT) return queued;
            }
            error_ = capture_error(MAA_WS_OK, 0, "");
            return capture_on_worker();
        });
    } catch (const winrt::hresult_error& e) {
        invalidate_session();
        error_ = framepool_error("FramePool worker failed", e.code());
        return {};
    }
}

std::optional<Image> FramePoolScreencap::capture_on_worker()
{
    if (!hwnd_) {
        LogError << "hwnd_ is nullptr";
        return std::nullopt;
    }
    if (!cap_frame_pool_) {
        if (!init()) {
            LogError << "init failed";
            uninit();
            return std::nullopt;
        }
    }

    // 检查窗口大小是否变化，如果变化则重新创建 frame pool
    if (!check_and_handle_size_changed()) {
        LogError << "check_and_handle_size_changed failed";
        return std::nullopt;
    }

    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame = nullptr;

    try {
        if (refreshing_cache_) frame = cap_frame_pool_.TryGetNextFrame();
        else {
        // 先清空 FramePool 中可能残留的旧帧
        for (unsigned drained = 0; drained < 16; ++drained) {
            auto old_frame = cap_frame_pool_.TryGetNextFrame();
            if (!old_frame) break;
            old_frame.Close();
            if (drained == 15) {
                // A continuously refilling queue is not a no-update timeout;
                // never mask it with a replay or spin without a bound.
                error_ = capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CAPTURE, "FramePool queue did not drain within its frame budget", method_);
                uninit(); return {};
            }
        }

        // 等待新帧到来
        using namespace std::chrono_literals;
        auto start_time = std::chrono::steady_clock::now();
        while (duration_since(start_time) < 2000ms) {
            if (operation_ && !operation_->check(error_,method_)) return {};
            std::this_thread::sleep_for(2ms);
            frame = cap_frame_pool_.TryGetNextFrame();
            if (frame) {
                break;
            }
        }
        }
    }
    catch (const winrt::hresult_error& e) {
        LogError << "Failed to get frame" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
        error_ = framepool_error("FramePool failed to get a frame", e.code());
        uninit();
        return std::nullopt;
    }

    if (!frame) {
        if (source_closed_ && source_closed_->load()) {
            error_ = capture_error(MAA_WS_TARGET_CHANGED, MAA_WS_STAGE_CAPTURE, "FramePool capture item closed", method_);
            uninit(); return {};
        }
        const auto removed = d3d_device_->GetDeviceRemovedReason();
        if (FAILED(removed)) {
            error_ = capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CAPTURE, "FramePool device was lost", method_, removed, "HRESULT");
            uninit(); return {};
        }
        error_ = capture_error(MAA_WS_TIMEOUT, MAA_WS_STAGE_CAPTURE, "FramePool timed out", method_);
        return std::nullopt;
    }

    auto surface = frame.Surface();
    const auto native_time = frame.SystemRelativeTime().count();
    if (native_time <= native_sample_time_) {
        error_ = capture_error(MAA_WS_TIMEOUT, MAA_WS_STAGE_CAPTURE, "FramePool had no newer native sample", method_); return {};
    }
    if (!surface) {
        LogError << "frame.Surface() is null";
        return std::nullopt;
    }

    auto access = surface.try_as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    if (!access) {
        LogError << "Failed to get IDirect3DDxgiInterfaceAccess";
        return std::nullopt;
    }

    winrt::com_ptr<ID3D11Texture2D> texture = nullptr;
    HRESULT ret = access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), texture.put_void());
    if (FAILED(ret)) {
        LogError << "GetInterface ID3D11Texture2D failed" << VAR(ret);
        error_ = framepool_error("FramePool texture interface failed", ret);
        return std::nullopt;
    }

    D3D11_TEXTURE2D_DESC captured_desc {}; texture->GetDesc(&captured_desc);
    const auto content_size = frame.ContentSize();
    if (content_size.Width != last_capture_size_.first || content_size.Height != last_capture_size_.second
        || content_size.Width <= 0 || content_size.Height <= 0
        || captured_desc.Width != UINT(content_size.Width) || captured_desc.Height != UINT(content_size.Height)) {
        error_ = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CAPTURE, "FramePool delivered a texture from an old content size", method_);
        uninit(); return {};
    }

    if (!readable_texture_ && !init_texture(texture)) {
        LogError << "falied to init_texture";
        return std::nullopt;
    }
    d3d_context_->CopyResource(readable_texture_.get(), texture.get());

    D3D11_MAPPED_SUBRESOURCE mapped { 0 };
    ret = d3d_context_->Map(readable_texture_.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(ret)) {
        LogError << "Map failed" << VAR(ret);
        error_ = framepool_error("FramePool texture mapping failed", ret);
        return std::nullopt;
    }
    OnScopeLeave([&]() { d3d_context_->Unmap(readable_texture_.get(), 0); });

    Image raw = Image::wrap(texture_desc_.Height, texture_desc_.Width, 4, mapped.pData, mapped.RowPitch);

    RECT client_rect = { 0 };
    if (!GetClientRect(hwnd_, &client_rect)) {
        LogError << "GetClientRect failed";
        return std::nullopt;
    }

    POINT client_top_left = { client_rect.left, client_rect.top };
    if (!ClientToScreen(hwnd_, &client_top_left)) {
        LogError << "ClientToScreen failed";
        return std::nullopt;
    }

    // 使用 DWM 实际可视帧边界，排除 GetWindowRect 中包含的不可见 DWM 阴影区域，
    // 以正确计算 WGC 捕获画面中的边框偏移
    RECT frame_rect = { 0 };
    HRESULT hr = DwmGetWindowAttribute(hwnd_, DWMWA_EXTENDED_FRAME_BOUNDS, &frame_rect, sizeof(frame_rect));
    if (FAILED(hr)) {
        LogWarn << "DwmGetWindowAttribute failed, falling back to GetWindowRect" << VAR(hr);
        if (!GetWindowRect(hwnd_, &frame_rect)) {
            LogError << "GetWindowRect failed";
            return std::nullopt;
        }
    }

    int border_left = client_top_left.x - frame_rect.left;
    int border_top = client_top_left.y - frame_rect.top;

    int client_width = client_rect.right - client_rect.left;
    int client_height = client_rect.bottom - client_rect.top;

    if (border_left < 0 || border_top < 0 || border_left + client_width > raw.cols || border_top + client_height > raw.rows) {
        error_ = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CAPTURE, "FramePool content bounds do not fit buffer", method_);
        return std::nullopt;
    }

    Rect client_roi(border_left, border_top, client_width, client_height);
    Image image = raw.roi(client_roi);

    Image result = bgra_to_bgr(image);
    captured_bounds_ = frame_rect;
    native_sample_time_ = native_time;
    return result;
}

bool FramePoolScreencap::init()
{
    LogFunc;

    if (!hwnd_) {
        LogError << "hwnd_ is nullptr";
        return false;
    }

    HRESULT ret = S_OK;

    ret = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        d3d_device_.put(), nullptr, d3d_context_.put());

    if (FAILED(ret)) {
        LogError << "D3D11CreateDeviceAndSwapChain failed" << VAR(ret);
        error_ = framepool_error("FramePool device creation failed", ret);
        return false;
    }

    try {
        auto activation_factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
        auto interop_factory = activation_factory.try_as<IGraphicsCaptureItemInterop>();
        if (!interop_factory) {
            LogError << "Failed to get IGraphicsCaptureItemInterop";
            return false;
        }
        ret = interop_factory->CreateForWindow(
            hwnd_,
            winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
            winrt::put_abi(cap_item_));
    }
    catch (const winrt::hresult_error& e) {
        LogError << "Failed to create GraphicsCaptureItem" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
        error_ = framepool_error("FramePool capture item creation failed", e.code());
        return false;
    }
    if (FAILED(ret)) {
        LogError << "CreateForWindow GraphicsCaptureItem failed" << VAR(ret);
        error_ = framepool_error("FramePool window item creation failed", ret);
        return false;
    }

    if (!cap_item_) {
        LogError << "cap_item_ is null after CreateForWindow";
        return false;
    }
    source_closed_ = std::make_shared<std::atomic_bool>(false);
    closed_token_ = cap_item_.Closed([closed = source_closed_](auto&&, auto&&) { closed->store(true); });

    auto item_size = cap_item_.Size();
    if (item_size.Width <= 0 || item_size.Height <= 0) {
        LogError << "Invalid capture item size" << VAR(item_size.Width) << VAR(item_size.Height);
        return false;
    }

    if (!IsWindow(hwnd_) || !IsWindowVisible(hwnd_)) {
        LogError << "Window is no longer valid or visible";
        return false;
    }

    auto dxgi_device = d3d_device_.try_as<IDXGIDevice>();
    if (!dxgi_device) {
        LogError << "Failed to get IDXGIDevice";
        return false;
    }

    winrt::com_ptr<IInspectable> inspectable = nullptr;
    ret = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.get(), inspectable.put());
    if (FAILED(ret)) {
        LogError << "CreateDirect3D11DeviceFromDXGIDevice failed" << VAR(ret);
        error_ = framepool_error("FramePool device interop failed", ret);
        return false;
    }

    auto d3d_device_interop = inspectable.try_as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    if (!d3d_device_interop) {
        LogError << "Failed to get IDirect3DDevice";
        return false;
    }

    try {
        cap_frame_pool_ = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            d3d_device_interop,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            1,
            cap_item_.Size());
    }
    catch (const winrt::hresult_error& e) {
        LogError << "Direct3D11CaptureFramePool::Create failed" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
        error_ = framepool_error("FramePool creation failed", e.code());
        return false;
    }

    if (!cap_frame_pool_) {
        LogError << "Direct3D11CaptureFramePool::Create returned null";
        return false;
    }
    frame_ready_ = std::make_shared<std::atomic_bool>(false);
    arrived_token_ = cap_frame_pool_.FrameArrived([ready = frame_ready_](auto&&, auto&&) { ready->store(true); });

    try {
        cap_session_ = cap_frame_pool_.CreateCaptureSession(cap_item_);
    }
    catch (const winrt::hresult_error& e) {
        LogError << "CreateCaptureSession failed" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
        error_ = framepool_error("FramePool session creation failed", e.code());
        return false;
    }
    if (!cap_session_) {
        LogError << "CreateCaptureSession returned null";
        return false;
    }

    // 尝试关闭截图时的黄色边框（Windows 11 及部分 Win10 版本支持）
    try_disable_border();

    // 尝试关闭截图时的鼠标指针（Windows 10 2004 及以上支持）
    try_disable_cursor();

    // 尝试包含从属窗口（弹窗、工具提示等）
    try_include_secondary_windows();

    try {
        cap_session_.StartCapture();
    }
    catch (const winrt::hresult_error& e) {
        LogError << "StartCapture failed" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
        error_ = framepool_error("FramePool session start failed", e.code());
        return false;
    }

    // 记录初始窗口大小
    if (cap_item_) {
        auto size = cap_item_.Size();
        last_capture_size_.first = size.Width;
        last_capture_size_.second = size.Height;
    }

    return true;
}

void FramePoolScreencap::uninit()
{
    invalidate_session();
    native_sample_time_ = 0;
    if (cap_item_ && source_closed_) { cap_item_.Closed(closed_token_); source_closed_.reset(); }
    if (cap_session_) {
        try {
            cap_session_.Close();
        }
        catch (const winrt::hresult_error& e) {
            LogWarn << "cap_session_.Close() failed" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
        }
        cap_session_ = nullptr;
    }

    if (cap_frame_pool_) {
        if (frame_ready_) { cap_frame_pool_.FrameArrived(arrived_token_); frame_ready_.reset(); }
        try {
            cap_frame_pool_.Close();
        }
        catch (const winrt::hresult_error& e) {
            LogWarn << "cap_frame_pool_.Close() failed" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
        }
        cap_frame_pool_ = nullptr;
    }

    readable_texture_ = nullptr;
    cap_item_ = nullptr;
    texture_desc_ = { 0 };
    last_capture_size_ = { };
    d3d_context_ = nullptr;
    d3d_device_ = nullptr;
}

bool FramePoolScreencap::check_and_handle_size_changed()
{
    if (!cap_item_) {
        return true;
    }

    if (!IsWindow(hwnd_)) {
        LogError << "Window is no longer valid";
        return false;
    }

    winrt::Windows::Graphics::SizeInt32 current_size { };
    try {
        current_size = cap_item_.Size();
    }
    catch (const winrt::hresult_error& e) {
        LogError << "Failed to get capture item size" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
        error_ = framepool_error("FramePool item size query failed", e.code());
        uninit();
        return false;
    }
    // 如果窗口大小没有变化，直接返回
    if (current_size.Width == last_capture_size_.first && current_size.Height == last_capture_size_.second) {
        return true;
    }

    LogInfo << "Window size changed, recreating frame pool" << VAR(current_size.Width) << VAR(current_size.Height)
            << VAR(last_capture_size_.first) << VAR(last_capture_size_.second);

    // 完全重新初始化以适应新的窗口大小
    uninit();
    if (!init()) {
        LogError << "reinit failed after size change";
        return false;
    }

    return true;
}

bool FramePoolScreencap::init_texture(winrt::com_ptr<ID3D11Texture2D> raw_texture)
{
    LogFunc;

    if (!d3d_device_ || !raw_texture) {
        LogError << "handle is null";
        return false;
    }

    raw_texture->GetDesc(&texture_desc_);

    texture_desc_.BindFlags = 0;
    texture_desc_.MiscFlags = 0;
    texture_desc_.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    texture_desc_.Usage = D3D11_USAGE_STAGING;

    HRESULT ret = d3d_device_->CreateTexture2D(&texture_desc_, NULL, readable_texture_.put());
    if (FAILED(ret)) {
        LogError << "CreateTexture2D failed" << VAR(ret);
        error_ = framepool_error("FramePool staging texture creation failed", ret);
        return false;
    }

    return true;
}

void FramePoolScreencap::try_disable_border()
{
    LogFunc;

    using namespace winrt::Windows::Foundation::Metadata;
    using namespace winrt::Windows::Graphics::Capture;

    // GraphicsCaptureAccess 和 IsBorderRequired 在 UniversalApiContract v10.0 (Windows 10 2004) 引入
    if (!ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 10)) {
        LogInfo << "UniversalApiContract v10 not present, border toggle not supported";
        return;
    }

    if (!ApiInformation::IsTypePresent(L"Windows.Graphics.Capture.GraphicsCaptureAccess")) {
        LogInfo << "GraphicsCaptureAccess not present, border toggle not supported";
        return;
    }

    if (!ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
        LogInfo << "IsBorderRequired property not supported on this system";
        return;
    }

    try {
        auto op = GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless);
        auto status = op.wait_for(std::chrono::seconds(5));
        if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
            LogWarn << "RequestAccessAsync did not complete in time";
            return;
        }

        auto access_result = op.GetResults();
        if (access_result != winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus::Allowed) {
            LogWarn << "Borderless capture access not granted:" << static_cast<int>(access_result);
            return;
        }

        cap_session_.IsBorderRequired(false);
        LogInfo << "Capture border disabled successfully";
    }
    catch (const winrt::hresult_error& e) {
        LogWarn << "Failed to disable capture border" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
    }
}

void FramePoolScreencap::try_disable_cursor()
{
    LogFunc;

    using namespace winrt::Windows::Foundation::Metadata;

    if (!ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled")) {
        LogInfo << "IsCursorCaptureEnabled property not supported on this system";
        return;
    }

    try {
        cap_session_.IsCursorCaptureEnabled(false);
        LogInfo << "Cursor capture disabled successfully";
    }
    catch (const winrt::hresult_error& e) {
        LogWarn << "Failed to disable cursor capture" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
    }
}

void FramePoolScreencap::try_include_secondary_windows()
{
    LogFunc;

    using namespace winrt::Windows::Foundation::Metadata;

    if (!ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IncludeSecondaryWindows")) {
        LogInfo << "IncludeSecondaryWindows property not supported on this system";
        return;
    }

    try {
        cap_session_.IncludeSecondaryWindows(false);
        LogInfo << "Secondary windows capture enabled successfully";
    }
    catch (const winrt::hresult_error& e) {
        LogWarn << "Failed to enable secondary windows capture" << VAR(e.code()) << VAR(winrt::to_string(e.message()));
    }
}

MWS_NS_END

#endif // MWS_FRAMEPOOL_AVAILABLE
