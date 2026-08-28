#include "maa-capture/MacOS/ScreenCaptureKitScreencap.h"

#include "maa-capture/Image.hpp"
#include "maa-capture/Logger.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <ScreenCaptureKit/ScreenCaptureKit.h>

MWS_NS_BEGIN

// 将 CMSampleBuffer 中的 BGRA 像素缓冲区转换为 BGR Image
static Image image_from_sample_buffer(CMSampleBufferRef sample_buffer)
{
    CVImageBufferRef pixel_buffer = CMSampleBufferGetImageBuffer(sample_buffer);
    if (!pixel_buffer) {
        return {};
    }

    CVPixelBufferLockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);

    void* base = CVPixelBufferGetBaseAddress(pixel_buffer);
    int width = static_cast<int>(CVPixelBufferGetWidth(pixel_buffer));
    int height = static_cast<int>(CVPixelBufferGetHeight(pixel_buffer));
    size_t bytes_per_row = CVPixelBufferGetBytesPerRow(pixel_buffer);

    Image result;
    if (base && width > 0 && height > 0) {
        // 锁定期间的 base 由 CVPixelBuffer 持有，转换立即完成深拷贝
        Image raw_image = Image::wrap(height, width, 4, base, bytes_per_row);
        cvt_bgra_to_bgr(raw_image, result);
    }

    CVPixelBufferUnlockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
    return result;
}

std::optional<Image> ScreenCaptureKitScreencap::screencap()
{
    if (window_id_ != 0) {
        return screencap_window(window_id_);
    }
    return screencap_display();
}

std::optional<Image> ScreenCaptureKitScreencap::screencap_window(uint32_t wid)
{
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block Image captured_image;
    __block bool capture_success = false;

    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent* content, NSError* error) {
        if (error || !content) {
            LogError << "Failed to get shareable content";
            dispatch_semaphore_signal(semaphore);
            return;
        }

        SCWindow* target_window = nil;
        for (SCWindow* window in content.windows) {
            if (window.windowID == wid) {
                target_window = window;
                break;
            }
        }

        if (!target_window) {
            LogError << "Target window not found";
            dispatch_semaphore_signal(semaphore);
            return;
        }

        SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:target_window];
        if (!filter) {
            LogError << "Failed to create content filter";
            dispatch_semaphore_signal(semaphore);
            return;
        }

        SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
        config.width = target_window.frame.size.width;
        config.height = target_window.frame.size.height;
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        config.colorSpaceName = kCGColorSpaceSRGB;
        config.showsCursor = NO;

        if (@available(macOS 14.0, *)) {
            [SCScreenshotManager
                captureSampleBufferWithFilter:filter
                                configuration:config
                            completionHandler:^(CMSampleBufferRef sampleBuffer, NSError* err) {
                                if (!err && sampleBuffer) {
                                    captured_image = image_from_sample_buffer(sampleBuffer);
                                    capture_success = true;
                                }
                                else {
                                    LogError << "Screenshot failed";
                                }
                                [filter release];
                                [config release];
                                dispatch_semaphore_signal(semaphore);
                            }];
        }
        else {
            LogError << "macOS 14.0 or later required for ScreenCaptureKit";
            [filter release];
            [config release];
            dispatch_semaphore_signal(semaphore);
        }
    }];

    if (dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0 && capture_success) {
        return captured_image;
    }
    return std::nullopt;
}

std::optional<Image> ScreenCaptureKitScreencap::screencap_display()
{
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block Image captured_image;
    __block bool capture_success = false;

    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent* content, NSError* error) {
        if (error || !content || content.displays.count == 0) {
            LogError << "Failed to get shareable content or no display found";
            dispatch_semaphore_signal(semaphore);
            return;
        }

        SCDisplay* display = content.displays[0];
        SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
        SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
        config.width = display.width;
        config.height = display.height;
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        config.colorSpaceName = kCGColorSpaceSRGB;
        config.showsCursor = NO;

        if (@available(macOS 14.0, *)) {
            [SCScreenshotManager
                captureSampleBufferWithFilter:filter
                                configuration:config
                            completionHandler:^(CMSampleBufferRef sampleBuffer, NSError* err) {
                                if (!err && sampleBuffer) {
                                    captured_image = image_from_sample_buffer(sampleBuffer);
                                    capture_success = true;
                                }
                                else {
                                    LogError << "Screenshot failed";
                                }
                                [filter release];
                                [config release];
                                dispatch_semaphore_signal(semaphore);
                            }];
        }
        else {
            LogError << "macOS 14.0 or later required for ScreenCaptureKit";
            [filter release];
            [config release];
            dispatch_semaphore_signal(semaphore);
        }
    }];

    if (dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0 && capture_success) {
        return captured_image;
    }
    return std::nullopt;
}

MWS_NS_END
