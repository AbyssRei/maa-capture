#include "maa-capture/MacOS/ScreenCaptureKitScreencap.h"
#include "MacCaptureInternal.hpp"
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include <chrono>
#include <cmath>
namespace mws {
std::optional<Image> ScreenCaptureKitScreencap::capture_image() {
    @autoreleasepool {
        if (@available(macOS 14.0, *)) {
            auto content = detail::shareable_content(error_); if (!content) return {};
            SCContentFilter* filter = detail::source_filter(content->value,target_,source_,error_); if (!filter) return {};
            const auto r = filter.contentRect;
            const auto scale = filter.pointPixelScale;
            if (!std::isfinite(scale) || scale<=0 || std::abs(r.size.width*scale-std::round(r.size.width*scale))>1e-6 || std::abs(r.size.height*scale-std::round(r.size.height*scale))>1e-6) {
                [filter release]; error_ = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY,MAA_WS_STAGE_CAPTURE,"Filter content does not map to an integral native grid",method_);return {};
            }
            if (std::llround(r.size.width * scale) != source_.pixel_width || std::llround(r.size.height * scale) != source_.pixel_height) {
                [filter release]; error_ = capture_error(MAA_WS_LAYOUT_CHANGED, MAA_WS_STAGE_CAPTURE, "Filter geometry changed", method_); return {};
            }
            SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
            config.width = source_.pixel_width; config.height = source_.pixel_height;
            config.pixelFormat = kCVPixelFormatType_32BGRA; config.colorSpaceName = kCGColorSpaceSRGB; config.showsCursor = NO;
            config.ignoreShadowsSingleWindow = YES;
            auto state = std::make_shared<detail::AsyncResult<Image>>();
            // Callback retains only its own shared state, never this or a stack reference.
            [SCScreenshotManager captureSampleBufferWithFilter:filter configuration:config completionHandler:^(CMSampleBufferRef sample, NSError* err) {
                std::lock_guard lock(state->mutex);
                try {
                if (err || !sample) {
                    const bool denied = err && [err.domain isEqualToString:SCStreamErrorDomain] && err.code == SCStreamErrorUserDeclined;
                    state->error = capture_error(denied ? MAA_WS_PERMISSION_DENIED : MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CAPTURE, "SCScreenshotManager failed", MAA_WS_METHOD_SCREENCAPTUREKIT, err ? err.code : 0, err ? err.domain.UTF8String : "");
                } else {
                    CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sample);
                    if (pixels && CVPixelBufferGetPixelFormatType(pixels) == kCVPixelFormatType_32BGRA && CVPixelBufferLockBaseAddress(pixels,kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess) {
                        struct Unlock { CVPixelBufferRef pixels; ~Unlock() { CVPixelBufferUnlockBaseAddress(pixels,kCVPixelBufferLock_ReadOnly); } } unlock { pixels };
                        auto raw = Image::wrap(int(CVPixelBufferGetHeight(pixels)),int(CVPixelBufferGetWidth(pixels)),4,CVPixelBufferGetBaseAddress(pixels),CVPixelBufferGetBytesPerRow(pixels));
                        state->value = bgra_to_bgr(raw);
                    }
                }
                } catch (...) { state->error = capture_error(MAA_WS_INTERNAL_ERROR,MAA_WS_STAGE_CAPTURE,"Exception in screenshot completion"); }
                state->done = true; state->ready.notify_one();
            }];
            [filter release]; [config release];
            std::unique_lock lock(state->mutex);
            if (!state->ready.wait_for(lock,std::chrono::seconds(5),[&]{ return state->done; })) {
                error_ = capture_error(MAA_WS_TIMEOUT, MAA_WS_STAGE_CAPTURE, "SCScreenshotManager timed out",method_); return {};
            }
            if (state->error.code || state->value.empty()) { error_ = state->error; return {}; }
            return state->value;
        }
        error_ = capture_error(MAA_WS_UNSUPPORTED_OPTION,MAA_WS_STAGE_CAPTURE,"macOS 14 or newer is required",method_); return {};
    }
}
}
