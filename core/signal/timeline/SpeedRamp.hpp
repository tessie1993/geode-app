#pragma once
#include <cstdint>
#include <vector>

#include "signal/timeline/Keyframes.hpp"

namespace geode::signal::timeline {

// A keyframed clip-speed track turned into the piecewise-constant speeds Media3's SpeedProvider asks for
// (core/signal/timeline). Keys sit in output (timeline) time; the provider is asked in source time, so the
// track is sampled along the clip every stepMs and each step's source consumption integrated to find where
// that step starts in the source. Runs of the same speed merge into one segment.
//
// Ported from the Kotlin export/SpeedRamp with the same float arithmetic per step (step µs as a float times
// the speed, truncated), so a ramp's source span does not move with the port.
class SpeedRamp {
public:
    static constexpr float kMinSpeed = 0.25f;
    static constexpr float kMaxSpeed = 4.0f;
    static constexpr int64_t kDefaultStepMs = 40;

    // Empty (segments() empty, source span 0) when the track has no keys. A non-scalar key reads as 1x.
    static SpeedRamp fromTrack(const Track& track, int64_t clipStartMs, int64_t clipDurationMs,
                               int64_t stepMs = kDefaultStepMs);

    bool empty() const { return startsUs_.empty(); }
    const std::vector<int64_t>& startsUs() const { return startsUs_; }
    const std::vector<float>& speeds() const { return speeds_; }
    int64_t sourceSpanUs() const { return sourceSpanUs_; }

    // The speed at source time timeUs (1x when empty).
    float speedAt(int64_t timeUs) const;
    // Where the next segment starts after timeUs, or -1 when none does (Media3's C.TIME_UNSET).
    int64_t nextChangeUs(int64_t timeUs) const;

private:
    int segmentAt(int64_t timeUs) const;

    std::vector<int64_t> startsUs_;
    std::vector<float> speeds_;
    int64_t sourceSpanUs_ = 0;
};

// A clip's fixed speed as read from a saved project, made safe for Media3: NaN, infinite, zero or negative
// is 1x, anything else clamped to kMinSpeed..kMaxSpeed. The project JSON reader had no clamp, so a hand-
// edited or imported project could hand Media3 a speed of 0 and a source span of 0.
float sanitizeClipSpeed(float speed);

}  // namespace geode::signal::timeline
