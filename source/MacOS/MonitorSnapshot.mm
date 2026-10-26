#include "MacCaptureInternal.hpp"
#import <AppKit/AppKit.h>
#include <cmath>
#include <chrono>
#include <cerrno>
#include <libproc.h>
#include <sys/proc_info.h>
namespace mws::detail {
std::shared_ptr<SharedContent> shareable_content(MaaWSError& error) {
    auto state = std::make_shared<AsyncResult<std::shared_ptr<SharedContent>>>();
    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent* content, NSError* err) {
        std::lock_guard lock(state->mutex);
        try {
        if (err || !content) {
            // Only explicit system evidence is classified as permission denial.
            const bool denied = err && [err.domain isEqualToString:SCStreamErrorDomain] && err.code == SCStreamErrorUserDeclined;
            state->error = capture_error(denied ? MAA_WS_PERMISSION_DENIED : MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_ENUMERATE,
                "SCShareableContent failed", 0, err ? err.code : 0, err ? err.domain.UTF8String : "");
        } else {
            state->value = std::make_shared<SharedContent>();
            state->value->value = [content retain];
        }
        } catch (...) { state->error = capture_error(MAA_WS_INTERNAL_ERROR,MAA_WS_STAGE_ENUMERATE,"Exception in shareable content completion"); }
        state->done = true; state->ready.notify_one();
    }];
    std::unique_lock lock(state->mutex);
    if (!state->ready.wait_for(lock, std::chrono::seconds(3), [&] { return state->done; })) {
        error = capture_error(MAA_WS_TIMEOUT, MAA_WS_STAGE_ENUMERATE, "SCShareableContent timed out"); return {};
    }
    error = state->error; return state->value;
}
std::optional<std::vector<MaaWSMonitorInfo>> platform_monitors(MaaWSError& e) {
    @autoreleasepool {
        if (@available(macOS 14.0, *)) {
            auto content = shareable_content(e); if (!content) return {};
            std::vector<MaaWSMonitorInfo> result;
            for (SCDisplay* d in content->value.displays) {
                MaaWSMonitorInfo i {}; i.struct_size = sizeof(i); i.native_id = d.displayID;
                i.primary = d.displayID == CGMainDisplayID(); i.identity_scope = MAA_WS_IDENTITY_SESSION;
                CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(d.displayID);
                if (uuid) {
                    CFStringRef text = CFUUIDCreateString(nullptr, uuid);
                    char buffer[128] {};
                    if (text && CFStringGetCString(text, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                        copy_text(i.monitor_key, "macos:" + std::string(buffer)); i.identity_scope = MAA_WS_IDENTITY_PERSISTENT;
                    }
                    if (text) CFRelease(text); CFRelease(uuid);
                }
                if (!i.monitor_key[0]) copy_text(i.monitor_key, "macos:session:" + std::to_string(d.displayID));
                if (CGDisplayIsInMirrorSet(d.displayID)) i.identity_scope = MAA_WS_IDENTITY_AMBIGUOUS;
                SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:d excludingWindows:@[]];
                const auto r = CGDisplayBounds(d.displayID);
                i.desktop_rect = {r.origin.x,r.origin.y,r.size.width,r.size.height};
                i.coordinate_unit = MAA_WS_UNIT_POINT; i.coordinate_space = MAA_WS_SPACE_DESKTOP; i.axis = MAA_WS_AXIS_X_RIGHT_Y_DOWN;
                i.scale_x = i.scale_y = filter.pointPixelScale;
                i.pixel_width = std::llround(filter.contentRect.size.width * i.scale_x); i.pixel_height = std::llround(filter.contentRect.size.height * i.scale_y);
                i.rotation = uint32_t(std::llround(CGDisplayRotation(d.displayID))) % 360;
                i.valid_fields = MAA_WS_VALID_IDENTITY | MAA_WS_VALID_GEOMETRY | MAA_WS_VALID_SCALE | MAA_WS_VALID_ROTATION;
                [filter release];
                for (NSScreen* screen in NSScreen.screens) {
                    if ([[screen.deviceDescription objectForKey:@"NSScreenNumber"] unsignedIntValue] == d.displayID) { copy_text(i.name, screen.localizedName.UTF8String ?: ""); break; }
                }
                result.push_back(i);
            }
            return result;
        }
        e = capture_error(MAA_WS_UNSUPPORTED_OPTION, MAA_WS_STAGE_ENUMERATE, "macOS 14 or newer is required"); return {};
    }
}
SCContentFilter* source_filter(SCShareableContent* content, const CaptureTarget& target, const MaaWSFrameInfo& source, MaaWSError& e) {
    if (target.type == MAA_WS_TARGET_MONITOR) {
        for (SCDisplay* d in content.displays) if (d.displayID == source.native_id) return [[SCContentFilter alloc] initWithDisplay:d excludingWindows:@[]];
    } else {
        for (SCWindow* w in content.windows) if (w.windowID == target.window_id && uint64_t(w.owningApplication.processID) == source.process_id)
            return [[SCContentFilter alloc] initWithDesktopIndependentWindow:w];
    }
    e = capture_error(MAA_WS_TARGET_NOT_FOUND, MAA_WS_STAGE_CAPTURE, "Requested ScreenCaptureKit source is absent"); return nil;
}
bool resolve_source(const CaptureTarget& t, MaaWSFrameInfo& i, uint64_t& identity, MaaWSError& e) {
    @autoreleasepool {
        if (@available(macOS 14.0, *)) {
            auto snapshot = MonitorSnapshot::create(e); if (!snapshot) return false;
            i = {}; i.struct_size = sizeof(i); i.target_type = t.type; i.scope = target_scope(t);
            i.layout_before = snapshot->layout_token; i.coordinate_unit = MAA_WS_UNIT_POINT; i.coordinate_space = MAA_WS_SPACE_DESKTOP;
            i.axis = MAA_WS_AXIS_X_RIGHT_Y_DOWN; i.pixel_semantics = MAA_WS_PIXELS_NATIVE; i.verification = MAA_WS_VERIFY_SAMPLED; i.limitations = MAA_WS_LIMIT_NONATOMIC;
            if (t.type == MAA_WS_TARGET_MONITOR) {
                const MaaWSMonitorInfo* found = nullptr;
                for (const auto& m : snapshot->items) if (!std::strcmp(m.monitor_key,t.source_key)) {
                    if (found || m.identity_scope == MAA_WS_IDENTITY_AMBIGUOUS) { e = capture_error(MAA_WS_TARGET_AMBIGUOUS, MAA_WS_STAGE_CAPTURE, "Monitor key is ambiguous"); return false; }
                    found = &m;
                }
                if (!found) { e = capture_error(MAA_WS_TARGET_NOT_FOUND, MAA_WS_STAGE_CAPTURE, "Monitor key is absent"); return false; }
                i.native_id = found->native_id; i.desktop_rect = found->desktop_rect;
                i.scale_x = found->scale_x; i.scale_y = found->scale_y; i.pixel_width = found->pixel_width; i.pixel_height = found->pixel_height;
                i.valid_fields = found->valid_fields | MAA_WS_VALID_MONITOR; i.monitor = *found;
                i.rotation = found->rotation; copy_text(i.source_key,found->monitor_key); return true;
            }
            auto content = shareable_content(e); if (!content) return false;
            for (SCWindow* w in content->value.windows) if (w.windowID == t.window_id) {
                const uint64_t pid = w.owningApplication.processID;
                struct proc_bsdinfo process {};
                if (proc_pidinfo(int(pid),PROC_PIDTBSDINFO,0,&process,sizeof(process)) != sizeof(process)) {
                    const auto native=errno;
                    e = capture_error(native == EACCES || native == EPERM ? MAA_WS_PERMISSION_DENIED : MAA_WS_CAPTURE_FAILED,MAA_WS_STAGE_CAPTURE,"Cannot verify window process",0,native,"POSIX");return false;
                }
                const uint64_t birth=uint64_t(process.pbi_start_tvsec)*1000000+process.pbi_start_tvusec;
                if ((t.process_id && t.process_id != pid) || (identity && identity != birth)) { e = capture_error(MAA_WS_TARGET_CHANGED, MAA_WS_STAGE_CAPTURE, "Window owner changed"); return false; }
                identity = birth;
                // Reject every mixed-scale intersection; one affine mapping cannot describe it.
                double scale = 0;
                for (const auto& m : snapshot->items) {
                    CGRect r = CGRectMake(m.desktop_rect.x,m.desktop_rect.y,m.desktop_rect.width,m.desktop_rect.height);
                    if (CGRectIntersectsRect(r,w.frame)) {
                        if (scale && scale != m.scale_x) { e = capture_error(MAA_WS_UNSUPPORTED_GEOMETRY, MAA_WS_STAGE_CAPTURE, "Window intersects displays with different pixel scales"); return false; }
                        scale = m.scale_x;
                    }
                }
                SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:w];
                auto r = filter.contentRect;
                i.native_id = w.windowID; i.process_id = pid;
                i.desktop_rect = {w.frame.origin.x,w.frame.origin.y,r.size.width,r.size.height};
                i.scale_x = i.scale_y = filter.pointPixelScale;
                i.pixel_width = std::llround(r.size.width * i.scale_x); i.pixel_height = std::llround(r.size.height * i.scale_y);
                i.valid_fields = MAA_WS_VALID_IDENTITY | MAA_WS_VALID_GEOMETRY | MAA_WS_VALID_SCALE;
                copy_text(i.source_key, "macos:window:" + std::to_string(w.windowID) + ":" + std::to_string(pid) + ":" + std::to_string(birth));
                [filter release]; return true;
            }
            e = capture_error(MAA_WS_TARGET_NOT_FOUND, MAA_WS_STAGE_CAPTURE, "Window is absent"); return false;
        }
        e = capture_error(MAA_WS_UNSUPPORTED_OPTION, MAA_WS_STAGE_CAPTURE, "macOS 14 or newer is required"); return false;
    }
}
}
