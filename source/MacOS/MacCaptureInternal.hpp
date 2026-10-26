#pragma once
#include "CaptureInternal.hpp"
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <condition_variable>
#include <memory>
#include <mutex>
namespace mws::detail {
template<class T> struct AsyncResult {
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    T value {};
    MaaWSError error = capture_error(MAA_WS_OK, 0, "");
};
struct SharedContent {
    SCShareableContent* value = nil;
    ~SharedContent() { [value release]; }
};
std::shared_ptr<SharedContent> shareable_content(MaaWSError& error);
SCContentFilter* source_filter(SCShareableContent* content, const CaptureTarget& target, const MaaWSFrameInfo& source, MaaWSError& error);
}