#include "signal/timeline/SpeedRamp.hpp"

#include <algorithm>
#include <cmath>

namespace geode::signal::timeline {

namespace {
constexpr float kSameSpeed = 1e-4f;

float clampSpeed(float v) { return std::clamp(v, SpeedRamp::kMinSpeed, SpeedRamp::kMaxSpeed); }

float speedOf(const Track& track, int64_t timelineMs) {
    Value v;
    if (!track.valueAt(timelineMs, &v) || v.kind != ValueKind::Scalar || std::isnan(v.v[0])) return 1.0f;
    return clampSpeed(v.v[0]);
}
}  // namespace

SpeedRamp SpeedRamp::fromTrack(const Track& track, int64_t clipStartMs, int64_t clipDurationMs, int64_t stepMs) {
    SpeedRamp r;
    if (track.empty()) return r;
    const int64_t step = std::max<int64_t>(1, stepMs);
    int64_t sourceUs = 0;
    for (int64_t outMs = 0; outMs < clipDurationMs; outMs += step) {
        const int64_t thisStep = std::min(step, clipDurationMs - outMs);
        const float speed = speedOf(track, clipStartMs + outMs);
        if (r.speeds_.empty() || std::fabs(r.speeds_.back() - speed) > kSameSpeed) {
            r.startsUs_.push_back(sourceUs);
            r.speeds_.push_back(speed);
        }
        sourceUs += static_cast<int64_t>(static_cast<float>(thisStep * 1000) * speed);
    }
    r.sourceSpanUs_ = sourceUs;
    return r;
}

int SpeedRamp::segmentAt(int64_t timeUs) const {
    const auto after = std::upper_bound(startsUs_.begin(), startsUs_.end(), timeUs);
    const int index = static_cast<int>(after - startsUs_.begin()) - 1;
    return index < 0 ? 0 : index;
}

float SpeedRamp::speedAt(int64_t timeUs) const { return empty() ? 1.0f : speeds_[static_cast<size_t>(segmentAt(timeUs))]; }

int64_t SpeedRamp::nextChangeUs(int64_t timeUs) const {
    if (empty()) return -1;
    const size_t next = static_cast<size_t>(segmentAt(timeUs)) + 1;
    return next < startsUs_.size() ? startsUs_[next] : -1;
}

float sanitizeClipSpeed(float speed) {
    if (!std::isfinite(speed) || speed <= 0.0f) return 1.0f;
    return clampSpeed(speed);
}

}  // namespace geode::signal::timeline
