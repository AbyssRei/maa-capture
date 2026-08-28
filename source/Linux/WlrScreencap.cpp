#include "maa-capture/Linux/WlrScreencap.h"

#include <optional>

#include "maa-capture/Image.hpp"
#include "maa-capture/Logger.h"

MWS_NS_BEGIN

std::optional<Image> WlrScreencap::screencap()
{
    if (!client_->connected()) {
        return std::nullopt;
    }

    std::unique_ptr<zwlr_screencopy_frame_v1> screencopy_frame;
    screencopy_frame.reset(zwlr_screencopy_manager_v1_capture_output(screencopy_manager_.get(), 0, output_.get()));

    zwlr_screencopy_frame_v1_listener frame_listener = { };
    frame_listener.buffer = [](void* data, zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t w, uint32_t h, uint32_t stride) {
        LogDebug << "Received new buffer" << VAR(format) << VAR(w) << VAR(h) << VAR(stride);
        const auto self = static_cast<WlrScreencap*>(data);
        if (!self->check_buffer(format, w, h, stride)) {
            LogDebug << "Buffer format changed, recreating";
            if (!self->create_buffer(format, w, h, stride)) {
                LogError << "Failed to prepare buffer";
                return;
            }
        }
        zwlr_screencopy_frame_v1_copy(frame, self->buffer_obj_.get());
    };
    frame_listener.ready =
        [](void* data, zwlr_screencopy_frame_v1* frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, [[maybe_unused]] uint32_t tv_nsec) {
            std::ignore = frame; // do not destroy manual
            const auto self = static_cast<WlrScreencap*>(data);
            int64_t seconds = (static_cast<int64_t>(tv_sec_hi) << 32) + tv_sec_lo;
            LogDebug << "Captured frame on" << VAR(seconds);
            self->capture_waiting_ = false;
            self->capture_successful_ = true;
        };
    frame_listener.failed = [](void* data, zwlr_screencopy_frame_v1* frame) {
        std::ignore = frame; // do not destroy manual
        const auto self = static_cast<WlrScreencap*>(data);
        self->capture_waiting_ = false;
        self->capture_successful_ = false;
        LogError << "Failed to capture frame";
    };
    frame_listener.damage = [](void* data, zwlr_screencopy_frame_v1* frame, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        std::ignore = data;
        std::ignore = frame;
        std::ignore = x;
        std::ignore = y;
        std::ignore = w;
        std::ignore = h;
        // nop
    };
    frame_listener.buffer_done = [](void* data, zwlr_screencopy_frame_v1* frame) {
        std::ignore = data;
        std::ignore = frame;
        // nop
    };
    frame_listener.linux_dmabuf = [](void* data, zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t w, uint32_t h) {
        std::ignore = data;
        std::ignore = frame;
        std::ignore = format;
        std::ignore = w;
        std::ignore = h;
        // nop
    };
    frame_listener.flags = [](void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t flags) {
        std::ignore = data;
        std::ignore = frame;
        LogDebug << "Capture flag" << VAR(flags);
    };
    capture_waiting_ = true;
    capture_successful_ = false;
    zwlr_screencopy_frame_v1_add_listener(screencopy_frame.get(), &frame_listener, this);

    while (capture_waiting_) {
        if (!client_->process_requests()) {
            return std::nullopt;
        }
    }

    if (!capture_successful_) {
        LogError << "Failed to capture frame";
        return std::nullopt;
    }

    ColorConversionCode cvt_mode = COLOR_NONE;
    int channels = -1;
    switch (buffer_format_) { // TODO: Other possible format?
    case WL_SHM_FORMAT_XBGR8888:
    case WL_SHM_FORMAT_ABGR8888:
        cvt_mode = COLOR_RGBA2BGR;
        channels = 4;
        break;
    case WL_SHM_FORMAT_ARGB8888:
    case WL_SHM_FORMAT_XRGB8888:
        cvt_mode = COLOR_BGRA2BGR;
        channels = 4;
        break;
    case WL_SHM_FORMAT_RGB888:
        // wl_shm 的 RGB888 按小端内存序实际是 B,G,R，无需转换
        cvt_mode = COLOR_NONE;
        channels = 3;
        break;
    case WL_SHM_FORMAT_BGR888:
        cvt_mode = COLOR_RGB2BGR;
        channels = 3;
        break;
    default:
        LogError << "Unsupported wl_shm_format" << VAR(buffer_format_);
        return std::nullopt;
    }

    // 包装 memfd 缓冲（行距 = stride）；返回前必须深拷贝，
    // 否则下一次 create_buffer 重分配后调用方仍持有的图像会悬垂
    Image raw = Image::wrap(buffer_height_, buffer_width_, channels, buffer_->ptr(), static_cast<std::size_t>(buffer_stride_));
    if (raw.empty()) {
        LogError << "Failed to wrap shm buffer";
        return std::nullopt;
    }

    if (cvt_mode != COLOR_NONE) {
        LogDebug << "Converting buffer" << VAR(buffer_format_) << VAR(static_cast<int>(cvt_mode));
        Image converted;
        cvt_color(raw, converted, cvt_mode);
        return converted;
    }
    return raw.clone();
}

bool WlrScreencap::check_buffer(int format, int width, int height, int stride) const
{
    if (!buffer_obj_ && !buffer_) {
        return false;
    }
    if (width != buffer_width_ || height != buffer_height_ || stride != buffer_stride_ || format != buffer_format_) {
        return false;
    }
    return true;
}

bool WlrScreencap::create_buffer(int format, int width, int height, int stride)
{
    if (!client_->connected()) {
        return false;
    }

    if (!close_buffer()) {
        LogError << "Failed to close old buffer";
    }
    buffer_ = std::make_unique<MemfdBuffer>(width, height, stride);
    if (!buffer_->available()) {
        LogError << "Failed to create shm buffer";
        return false;
    }
    buffer_format_ = format;
    buffer_width_ = width;
    buffer_height_ = height;
    buffer_stride_ = stride;
    shm_pool_.reset(wl_shm_create_pool(shm_.get(), buffer_->fd(), buffer_->size()));
    buffer_obj_.reset(wl_shm_pool_create_buffer(shm_pool_.get(), 0, width, height, stride, format));
    if (!client_->process_requests()) {
        LogError << "Failed to create buffer objects";
        return false;
    }
    return true;
}

bool WlrScreencap::close_buffer()
{
    if (!client_->connected()) {
        if (buffer_) {
            buffer_.reset(); // Close memfd only
        }
        return true;
    }

    if (!buffer_) {
        return true;
    }
    LogDebug << "Closing buffer" << VAR_VOIDP(buffer_.get());
    buffer_obj_.reset();
    shm_pool_.reset();
    if (!client_->process_requests()) {
        LogError << "Failed to destroy buffer objects";
        return false;
    }
    buffer_.reset();
    return true;
}

MWS_NS_END
