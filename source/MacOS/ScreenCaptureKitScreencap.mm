#include "maa-capture/MacOS/ScreenCaptureKitScreencap.h"

#include "maa-capture/Image.hpp"
#include "maa-capture/Logger.h"

#include <ApplicationServices/ApplicationServices.h>
#include <AppKit/AppKit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <ScreenCaptureKit/ScreenCaptureKit.h>

MWS_NS_BEGIN

// ---------------------------------------------------------------------------
// 显示器枚举（与截屏单元同源：SCShareableContent.displays 顺序）
// ---------------------------------------------------------------------------

uint32_t sck_monitor_count()
{
    __block uint32_t count = 0;
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent* content, NSError* error) {
        if (!error && content) {
            count = static_cast<uint32_t>(content.displays.count);
        }
        else {
            LogWarn << "sck_monitor_count: failed to get shareable content (TCC permission?)";
        }
        dispatch_semaphore_signal(semaphore);
    }];
    dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));

    return count;
}

bool sck_monitor_info(uint32_t index, bool& primary, std::string& name)
{
    __block SCDisplay* found = nil;
    __block CGDirectDisplayID found_id = 0;
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent* content, NSError* error) {
        if (!error && content && index < content.displays.count) {
            found = content.displays[index];
            found_id = found.displayID;
        }
        dispatch_semaphore_signal(semaphore);
    }];
    dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));

    if (!found) {
        LogWarn << "sck_monitor_info: display not available" << VAR(index);
        return false;
    }

    primary = found_id == CGMainDisplayID();

    // SCDisplay 不暴露名称；按 displayID 匹配 NSScreen 取本地化名称
    NSString* ns_name = nil;
    for (NSScreen* screen in [NSScreen screens]) {
        NSNumber* number = [screen deviceDescription][@"NSScreenNumber"];
        if (number && [number unsignedIntValue] == found_id) {
            ns_name = [screen localizedName];
            break;
        }
    }
    if (ns_name) {
        const char* utf8 = [ns_name UTF8String];
        name = utf8 ? utf8 : "";
    }
    return true;
}

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

        if (display_index_ >= content.displays.count) {
            LogError << "display index out of range" << VAR(display_index_) << VAR(content.displays.count);
            dispatch_semaphore_signal(semaphore);
            return;
        }

        SCDisplay* display = content.displays[display_index_];
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
