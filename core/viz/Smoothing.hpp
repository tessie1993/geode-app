#pragma once
#include <algorithm>
#include <cmath>

namespace geode::viz {

// One-pole toward `target`, framerate independent, with its own rate for rising
// and falling. exp(-dt*hz) rather than a fixed per-frame fraction: the same
// wall-clock rise on a 30fps device and a 120fps one, so the look does not
// change with the frame rate the thermal governor happens to be pacing at.
inline float slewTo(float current, float target, float dt, float riseHz, float fallHz) {
    const float hz = target > current ? riseHz : fallHz;
    const float k = 1.0f - std::exp(-std::max(dt, 0.0f) * hz);
    return current + (target - current) * k;
}

// Shortest signed distance from `from` to `to` on a circle of `period`.
inline float wrappedDelta(float from, float to, float period) {
    const float half = period * 0.5f;
    float d = std::fmod(to - from + half, period);
    if (d < 0.0f) d += period;
    return d - half;
}

// An attack/release envelope for ONE-HOP impulses.
//
// The analyzer reports a transient, a kick, a drop as a value that is non-zero
// on exactly one hop and zero on the next. Feeding that straight into slewTo()
// does not produce an envelope: the target is 1 for a single frame, so an 8 Hz
// rise covers 13% of the gap at 60fps and the value never gets past 0.13. What
// a hit needs is a target that is HELD until the rise has reached it, and only
// then released - which is what this is.
//
// trigger() arms a peak; step() climbs toward it with a one-pole and, once
// within kArrive of it, lets go and decays. The climb is still a one-pole, so
// no single frame can move the value far: the flash protection the motion
// layer exists for is kept, the peak is just actually reached.
class HitEnvelope {
public:
    HitEnvelope(float riseHz, float fallHz) : riseHz_(riseHz), fallHz_(fallHz) {}

    float value() const { return value_; }

    // A second hit during the climb raises the peak rather than restarting it,
    // so a flam reads as one strong hit rather than as two weak ones.
    void trigger(float strength) { peak_ = std::max(peak_, std::clamp(strength, 0.0f, 1.0f)); }

    float step(float dt) {
        const float t = std::max(dt, 0.0f);
        if (peak_ > value_) {
            value_ += (peak_ - value_) * (1.0f - std::exp(-t * riseHz_));
            if (peak_ - value_ < kArrive) peak_ = 0.0f;
        } else {
            peak_ = 0.0f;
            value_ *= std::exp(-t * fallHz_);
        }
        return value_;
    }

    void reset() {
        value_ = 0.0f;
        peak_ = 0.0f;
    }

private:
    // How close to the armed peak counts as having arrived. A one-pole never
    // reaches its target, so without this the release would never start.
    static constexpr float kArrive = 0.04f;

    float riseHz_;
    float fallHz_;
    float value_ = 0.0f;
    float peak_ = 0.0f;
};

}  // namespace geode::viz
