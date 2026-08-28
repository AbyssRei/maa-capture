#pragma once

// ============================================================================
// ★ 外部回调截图接口（C++ 侧封装）
//
// 纯 C 契约见 MaaWSCustomScreencap.h（对应 MaaFramework
// MaaCustomControllerCallbacks::screencap 的"外部宿主实现截图"协议）。
// 本头文件在其之上提供 C++ 桥接：
//   CustomScreencapBridge : ScreencapBase —— 把「C 回调实现」包装成
//   「可继承截图单元」，从而能注册进 ScreencapMgr 参与测速选择。
//
// 用法：
//   1. 外部按 C 接口实现回调（见 sample/external_c_callback.cpp）
//   2. C++ 侧直接构造 CustomScreencapBridge，或经 C API 创建句柄
// ============================================================================

#include "MaaWSCustomScreencap.h" // 纯 C 外部回调契约 + C API

#include "Common.h"
#include "ScreencapBase.h"

MWS_NS_BEGIN

// ---- C++ 便捷别名 ----
using CustomScreencapCallbacks = MaaWSCustomScreencapCallbacks;
using ImageBuffer = MaaWSImageBuffer;

// ---- 桥接：C 回调 -> ScreencapBase 继承单元 ----

class CustomScreencapBridge : public ScreencapBase
{
public:
    CustomScreencapBridge(const MaaWSCustomScreencapCallbacks* callbacks, void* trans_arg)
        : callbacks_(callbacks)
        , trans_arg_(trans_arg)
    {
    }

    virtual ~CustomScreencapBridge() override = default;

public: // from ScreencapBase
    virtual std::optional<Image> screencap() override
    {
        if (!callbacks_ || !callbacks_->screencap) {
            return std::nullopt;
        }

        MaaWSImageBuffer buffer { };
        if (!callbacks_->screencap(trans_arg_, &buffer)) {
            return std::nullopt;
        }

        return buffer_to_mat(buffer);
    }

    virtual void inactive() override
    {
        if (callbacks_ && callbacks_->inactive) {
            callbacks_->inactive(trans_arg_);
        }
    }

public:
    bool connected() const
    {
        return callbacks_ && callbacks_->connected && callbacks_->connected(trans_arg_);
    }

    bool connect()
    {
        return callbacks_ && callbacks_->connect && callbacks_->connect(trans_arg_);
    }

    // 把回调写入的 ImageBuffer 转换为 BGR Image
    static Image buffer_to_mat(const MaaWSImageBuffer& buffer)
    {
        if (!buffer.data || buffer.width <= 0 || buffer.height <= 0) {
            return { };
        }
        if (buffer.channels != 3 && buffer.channels != 4) {
            return { };
        }

        const int step = buffer.step > 0 ? buffer.step : buffer.width * buffer.channels;
        // 包装回调写入的外部内存（不拥有），随后按通道数归一化
        Image img = Image::wrap(buffer.height, buffer.width, buffer.channels, buffer.data, step);

        if (buffer.channels == 4) {
            return bgra_to_bgr(img);
        }
        return img.clone();
    }

private:
    const MaaWSCustomScreencapCallbacks* callbacks_ = nullptr;
    void* trans_arg_ = nullptr;
};

MWS_NS_END