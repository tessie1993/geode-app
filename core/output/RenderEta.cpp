#include "output/RenderEta.hpp"

#include <cmath>
#include <limits>

namespace geode::output {

namespace {

// The nearest whole second, halves up, never negative and never past what an int holds.
int64_t wholeSeconds(float seconds) {
    const double rounded = std::floor(static_cast<double>(seconds) + 0.5);
    if (!(rounded > 0.0)) return 0;
    const double most = static_cast<double>(std::numeric_limits<int32_t>::max());
    return rounded >= most ? std::numeric_limits<int32_t>::max() : static_cast<int64_t>(rounded);
}

}  // namespace

int64_t RenderEta::sample(float progress, int64_t atMs, bool steady) {
    const float clamped = !(progress > 0.0f) ? 0.0f : (progress > 1.0f ? 1.0f : progress);
    if (!steady || clamped < latestProgress_) {
        reset();
        latestProgress_ = clamped;
        return kUnknown;
    }
    latestProgress_ = clamped;
    if (clamped >= 1.0f) return 0;
    if (steadySinceMs_ == kNone) steadySinceMs_ = atMs;
    if (baseMs_ == kNone) {
        if (atMs - steadySinceMs_ < kWarmupMs) return kUnknown;
        baseMs_ = atMs;
        baseProgress_ = clamped;
        return kUnknown;
    }
    const int64_t spanMs = atMs - baseMs_;
    const float gained = clamped - baseProgress_;
    if (spanMs < kMinSpanMs || gained < kMinProgress) return kUnknown;
    const float fresh = (1.0f - clamped) * static_cast<float>(spanMs) / gained / 1000.0f;
    float next = fresh;
    if (shown_) {
        const int64_t dtMs = atMs > shownAtMs_ ? atMs - shownAtMs_ : 0;
        const float countedDown = shownSeconds_ - static_cast<float>(dtMs) / 1000.0f;
        const float held = countedDown > 0.0f ? countedDown : 0.0f;
        const float decay = -static_cast<float>(dtMs) / static_cast<float>(kSmoothingMs);
        const float pull = 1.0f - static_cast<float>(std::exp(static_cast<double>(decay)));
        next = held + (fresh - held) * pull;
    }
    shown_ = true;
    shownSeconds_ = next;
    shownAtMs_ = atMs;
    return wholeSeconds(next);
}

void RenderEta::reset() {
    steadySinceMs_ = kNone;
    baseMs_ = kNone;
    baseProgress_ = 0.0f;
    latestProgress_ = 0.0f;
    shown_ = false;
    shownSeconds_ = 0.0f;
    shownAtMs_ = 0;
}

namespace eta {

Wording wordingFor(int64_t secondsRemaining) {
    if (secondsRemaining < 30) return {kAlmostDone, 0};
    if (secondsRemaining < 90) return {kAboutAMinute, 0};
    if (secondsRemaining < 3600) return {kMinutes, static_cast<int>((secondsRemaining + 30) / 60)};
    // Past what an int holds the hour count stays at the most it can say.
    const int64_t most = std::numeric_limits<int32_t>::max();
    const int64_t hours = secondsRemaining > most * 3600 ? most : (secondsRemaining + 1800) / 3600;
    return {kHours, static_cast<int>(hours)};
}

}  // namespace eta

}  // namespace geode::output
