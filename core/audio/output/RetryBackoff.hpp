#pragma once

#include <atomic>
#include <cstdint>

namespace geode::audio::output {

// When the native playback output may be tried again after a stream failed for good. Not for the life of the
// process: a Bluetooth hand-over can fail one reopen, and the platform track would then play until the app
// was killed. The caller gives the clock (milliseconds on any monotonic clock), so this holds no time source
// and no Oboe, and a host test reaches all of it. Safe from any thread.
class RetryBackoff {
public:
    static constexpr int64_t kRetryAfterMs = 60'000;

    // A stream failed for good at `nowMs`.
    void noteFailed(int64_t nowMs) { failedAtMs_.store(nowMs, std::memory_order_relaxed); }

    // False while the last failure is less than kRetryAfterMs old; true when none ever happened.
    bool mayOpen(int64_t nowMs) const {
        const int64_t failedAt = failedAtMs_.load(std::memory_order_relaxed);
        return failedAt == kNever || nowMs - failedAt >= kRetryAfterMs;
    }

private:
    static constexpr int64_t kNever = INT64_MIN;
    std::atomic<int64_t> failedAtMs_{kNever};
};

}  // namespace geode::audio::output
