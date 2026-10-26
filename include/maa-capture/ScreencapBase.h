#pragma once
#include <optional>
#include <mutex>
#include "CaptureTypes.hpp"
#include "CaptureControl.hpp"
MWS_NS_BEGIN
class PseudoMinimizeHelper;
class ScreencapBase {
public:
    virtual ~ScreencapBase() = default;
    virtual std::optional<CaptureFrame> screencap() = 0;
    virtual void inactive() {}
    virtual bool release_resources() { inactive(); return true; }
    // Providers cannot independently re-resolve their source. Native backends
    // override this before delivery of a connection's retained first frame.
    virtual bool validate_pending_frame(const CaptureFrame&) { return true; }
    virtual void set_operation(std::shared_ptr<CaptureOperation> operation) { std::lock_guard lock(capture_mutex_); operation_ = std::move(operation); }
    virtual void enable_diagnostics(bool enabled) { diagnostics_ = enabled; }
    virtual const MaaWSError& last_error() const { return error_; }
protected:
    // Serializes native publication with optional backend cache refreshes.
    // It does not make a Manager safe for concurrent host calls.
    mutable std::recursive_mutex capture_mutex_;
    MaaWSError error_ = capture_error(MAA_WS_OK, 0, "");
    std::shared_ptr<CaptureOperation> operation_;
    bool diagnostics_ = false;
};
/* Shared native source transaction; pixel helpers copy while native buffers
 * are valid. Publication requires before/after verification of this source. */
class TargetScreencapBase : public ScreencapBase {
public:
    TargetScreencapBase(CaptureTarget target, uint64_t method, bool cache_timeout = false)
        : target_(std::move(target)), method_(method), cache_timeout_(cache_timeout) {}
    std::optional<CaptureFrame> screencap() override;
    virtual bool validate_source();
    bool validate_pending_frame(const CaptureFrame&) override;
protected:
    void invalidate_session();
    void invalidate_cache(const char* reason);
    bool validate_window_lifetime();
    virtual std::optional<Image> capture_image() = 0;
    const CaptureTarget target_;
    const uint64_t method_;
    MaaWSFrameInfo source_ {};
    uint64_t process_identity_ = 0;
    uint64_t capture_session_ = 1, next_sample_ = 0;
    const bool cache_timeout_;
    std::optional<CaptureFrame> cached_frame_;
    std::shared_ptr<PseudoMinimizeHelper> window_lifetime_;
    uint64_t replay_count_ = 0, invalidation_count_ = 0;
};
MWS_NS_END
