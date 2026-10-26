#pragma once
#include "CaptureTypes.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <limits>

namespace mws {
inline uint64_t capture_clock_ns() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
// Independently owned signal. Only cancellation requests are concurrent-safe;
// the Manager and its providers still have one serial host caller.
class CaptureCancellation {
public:
    bool request() {
        auto value = state_.load();
        while (value) {
            if (state_.compare_exchange_weak(value, value | 1)) return true;
        }
        return false;
    }
private:
    friend class ScreencapMgr;
    friend struct CaptureOperation;
    std::atomic<uint64_t> state_ {0};
};
struct CaptureOperation {
    const uint64_t started_ns = capture_clock_ns();
    uint64_t deadline_ns = UINT64_MAX, epoch = 0, policy_flags = 0, max_sample_age_ns = 0;
    uint32_t stage = MAA_WS_STAGE_CAPTURE;
    std::shared_ptr<CaptureCancellation> cancellation;
    void limit(uint64_t duration) {
        if (duration) deadline_ns = std::min(deadline_ns, duration > UINT64_MAX - started_ns ? UINT64_MAX : started_ns + duration);
    }
    bool check(MaaWSError& error, uint64_t method = 0) const {
        if (cancellation && cancellation->state_.load() == (epoch | 1)) {
            error = capture_error(MAA_WS_CANCELLED, stage, "Capture operation cancelled", method); return false;
        }
        if (capture_clock_ns() >= deadline_ns) {
            error = capture_error(MAA_WS_DEADLINE_EXCEEDED, stage, "Capture operation deadline exceeded", method); return false;
        }
        return true;
    }
    uint32_t poll_ms(uint32_t maximum = 20) const {
        const auto now = capture_clock_ns();
        if (now >= deadline_ns) return 0;
        return uint32_t(std::min<uint64_t>(maximum, (deadline_ns - now) / 1000000 + ((deadline_ns - now) % 1000000 != 0)));
    }
    bool accepts(const CaptureFrame& frame, MaaWSError& error) const {
        if (!check(error, frame.source.method)) return false;
        if (!(policy_flags & MAA_WS_POLICY_FRESH_ONLY) && !max_sample_age_ns) return true;
        const auto sampled = frame.sample.sampled_at_ns;
        if (!sampled) { error = capture_error(MAA_WS_UNSUPPORTED_OPTION, stage, "Native sample age is unknown", frame.source.method); return false; }
        const auto now = capture_clock_ns();
        if (sampled > now || ((policy_flags & MAA_WS_POLICY_FRESH_ONLY) &&
            ((frame.sample.flags & MAA_WS_SAMPLE_REUSED) || sampled < started_ns)) ||
            (max_sample_age_ns && now - sampled > max_sample_age_ns)) {
            error = capture_error(MAA_WS_STALE_FRAME, stage, "Frame does not satisfy sampling freshness policy", frame.source.method); return false;
        }
        return true;
    }
};
}
