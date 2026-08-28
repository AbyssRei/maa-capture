#include "maa-capture/CustomScreencap.h"

#include "maa-capture/Logger.h"
#include "maa-capture/ScreencapMgr.h"

MWS_NS_BEGIN

// 内部句柄载体（C++ 侧可直接使用 CustomScreencapBridge）
class CustomScreencapHandleImpl
{
public:
    CustomScreencapHandleImpl(const MaaWSCustomScreencapCallbacks* callbacks, void* trans_arg)
        : bridge_(callbacks, trans_arg)
    {
    }

    bool screencap(MaaWSImageBuffer* out_buffer)
    {
        auto opt = bridge_.screencap();
        if (!opt || out_buffer == nullptr) {
            return false;
        }

        // 持有拷贝：回调包装返回的临时 Image 在函数退出后失效，
        // 输出指针必须指向本对象长期持有的图像
        image_ = *opt;

        fill_buffer(*out_buffer, image_);
        return true;
    }

private:
    static void fill_buffer(MaaWSImageBuffer& out, const Image& image)
    {
        out.data = image.data;
        out.width = image.cols;
        out.height = image.rows;
        out.channels = image.channels();
        out.step = static_cast<int32_t>(image.step);
    }

private:
    CustomScreencapBridge bridge_;
    Image image_;
};

// 截图管理器 C API 的句柄载体
class ManagerHandleImpl
{
public:
    ManagerHandleImpl(void* hwnd, MaaWSMethod methods)
        : mgr_(hwnd)
    {
        mgr_.set_builtin_methods(static_cast<ScreencapMethod>(methods));
    }

    bool register_custom_unit(const char* name, const MaaWSCustomScreencapCallbacks* callbacks, void* trans_arg)
    {
        if (!name || !callbacks || !callbacks->screencap) {
            LogError << "Invalid custom unit" << VAR(name ? name : "(null)");
            return false;
        }

        std::shared_ptr<ScreencapBase> bridge = std::make_shared<CustomScreencapBridge>(callbacks, trans_arg);
        return mgr_.register_unit(name, std::move(bridge));
    }

    // Linux 截图目标配置（其他平台上 ScreencapMgr 内部不使用这些值）
    void set_pipewire_target(int32_t fd, uint32_t node_id)
    {
        mgr_.set_pipewire_target(fd, node_id);
    }

    void set_wlr_socket(const char* socket_name)
    {
        mgr_.set_wlr_socket(socket_name ? socket_name : "");
    }

    bool connect()
    {
        return mgr_.connect();
    }

    bool screencap(MaaWSImageBuffer* out_buffer)
    {
        if (out_buffer == nullptr) {
            LogError << "out_buffer is null";
            return false;
        }

        Image image;
        if (!mgr_.screencap(image)) {
            return false;
        }

        image_ = std::move(image);
        out_buffer->data = image_.data;
        out_buffer->width = image_.cols;
        out_buffer->height = image_.rows;
        out_buffer->channels = image_.channels();
        out_buffer->step = static_cast<int32_t>(image_.step);
        return true;
    }

    const char* selected_unit_name()
    {
        if (!mgr_.connected()) {
            return nullptr;
        }
        return mgr_.selected_unit_name().c_str();
    }

    void inactive()
    {
        mgr_.inactive();
    }

private:
    ScreencapMgr mgr_;
    Image image_;
};

MWS_NS_END

using namespace mws;

extern "C"
{
    MaaWSCustomScreencapHandle MaaWSCustomScreencapCreate(const MaaWSCustomScreencapCallbacks* callbacks, void* trans_arg)
    {
        if (!callbacks || !callbacks->screencap) {
            LogError << "Invalid callbacks (screencap is required)";
            return nullptr;
        }

        auto impl = std::make_unique<CustomScreencapHandleImpl>(callbacks, trans_arg);
        return impl.release();
    }

    void MaaWSCustomScreencapDestroy(MaaWSCustomScreencapHandle handle)
    {
        if (handle) {
            delete static_cast<CustomScreencapHandleImpl*>(handle);
        }
    }

    MaaWSBool MaaWSCustomScreencapScreencap(MaaWSCustomScreencapHandle handle, MaaWSImageBuffer* out_buffer)
    {
        if (!handle) {
            LogError << "handle is null";
            return 0;
        }

        auto impl = static_cast<CustomScreencapHandleImpl*>(handle);
        return impl->screencap(out_buffer) ? 1 : 0;
    }

    // ---- Manager C API ----

    MaaWSManagerHandle MaaWSManagerCreate(void* hwnd, MaaWSMethod methods)
    {
        auto impl = std::make_unique<ManagerHandleImpl>(hwnd, methods);
        return impl.release();
    }

    void MaaWSManagerDestroy(MaaWSManagerHandle handle)
    {
        if (handle) {
            delete static_cast<ManagerHandleImpl*>(handle);
        }
    }

    MaaWSBool MaaWSManagerSetPipeWireTarget(MaaWSManagerHandle handle, int32_t fd, uint32_t node_id)
    {
        if (!handle) {
            LogError << "handle is null";
            return 0;
        }
        static_cast<ManagerHandleImpl*>(handle)->set_pipewire_target(fd, node_id);
        return 1;
    }

    MaaWSBool MaaWSManagerSetWlrSocket(MaaWSManagerHandle handle, const char* socket_name)
    {
        if (!handle) {
            LogError << "handle is null";
            return 0;
        }
        static_cast<ManagerHandleImpl*>(handle)->set_wlr_socket(socket_name);
        return 1;
    }

    MaaWSBool MaaWSManagerRegisterCustomUnit(MaaWSManagerHandle handle, const char* name, const MaaWSCustomScreencapCallbacks* callbacks, void* trans_arg)
    {
        if (!handle) {
            LogError << "handle is null";
            return 0;
        }
        return static_cast<ManagerHandleImpl*>(handle)->register_custom_unit(name, callbacks, trans_arg) ? 1 : 0;
    }

    MaaWSBool MaaWSManagerConnect(MaaWSManagerHandle handle)
    {
        if (!handle) {
            LogError << "handle is null";
            return 0;
        }
        return static_cast<ManagerHandleImpl*>(handle)->connect() ? 1 : 0;
    }

    MaaWSBool MaaWSManagerScreencap(MaaWSManagerHandle handle, MaaWSImageBuffer* out_buffer)
    {
        if (!handle) {
            LogError << "handle is null";
            return 0;
        }
        return static_cast<ManagerHandleImpl*>(handle)->screencap(out_buffer) ? 1 : 0;
    }

    const char* MaaWSManagerSelectedUnitName(MaaWSManagerHandle handle)
    {
        if (!handle) {
            return nullptr;
        }
        return static_cast<ManagerHandleImpl*>(handle)->selected_unit_name();
    }

    void MaaWSManagerInactive(MaaWSManagerHandle handle)
    {
        if (handle) {
            static_cast<ManagerHandleImpl*>(handle)->inactive();
        }
    }
}