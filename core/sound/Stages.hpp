#pragma once
#include <algorithm>
#include <cmath>

// The small per-frame stages the sound engine is built from. Every one takes the frame's dt, so the same
// audio gives the same values whether frames come at 30, 60 or 120 per second.
namespace geode::sound {

inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// MilkDrop tunes its smoothing as a factor per frame at 30 fps. Keeping the decay per second constant,
// the factor for a frame of dt seconds is f0^(30·dt).
inline float perFrame(float f0, float dt) { return std::pow(f0, 30.0f * dt); }

// One-pole step toward x with time constant tau seconds; tau 0 follows at once.
inline float follow(float y, float x, float tau, float dt) {
    if (tau <= 0.0f) return x;
    return x + (y - x) * std::exp(-dt / tau);
}

// The core of the engine: a band's energy measured against its own running mean (~4 s), so 1.0 means
// "typical for this song right now" whatever the volume or mastering. Alongside it, a fast envelope
// (~20 ms rise, ~50 ms fall) over the same mean.
//
// Two departures from MilkDrop, both on purpose. The mean starts at the first sound it hears instead of
// at zero, which removes MilkDrop's start-up spike (10× at 30 fps, 20× at 60). And digital silence
// freezes the mean instead of letting it decay, so music returning after a pause reads 1-ish, not 50.
class RelativeLevel {
public:
    // `floor`: energy below it counts as none, so a band holding only rounding noise reads 0 rather than
    // a ratio of noise to noise that leaps about.
    void step(float inst, float dt, bool warm, bool silent, float floor = 0.0f) {
        if (silent || inst < floor) inst = 0.0f;
        const float r = perFrame(inst > att_ ? kAttRise : kAttFall, dt);
        att_ = r * att_ + (1.0f - r) * inst;
        if (!silent) {
            if (!primed_) {
                mean_ = inst;
                primed_ = true;
            } else {
                const float s = perFrame(warm ? kMeanWarm : kMeanSteady, dt);
                mean_ = s * mean_ + (1.0f - s) * inst;
            }
        }
        if (mean_ > kFloor) {
            ratio_ = inst / mean_;
            attRatio_ = att_ / mean_;
        } else {
            // Nothing heard in this band yet: MilkDrop's answer, "normal", if there is anything now.
            ratio_ = inst > 0.0f ? 1.0f : 0.0f;
            attRatio_ = ratio_;
        }
    }
    float ratio() const { return ratio_; }
    float attRatio() const { return attRatio_; }
    void reset() { *this = RelativeLevel(); }

private:
    static constexpr float kAttRise = 0.2f;      // ~21 ms
    static constexpr float kAttFall = 0.5f;      // ~48 ms
    static constexpr float kMeanWarm = 0.9f;     // ~0.32 s, while the engine settles
    static constexpr float kMeanSteady = 0.992f; // ~4.15 s
    static constexpr float kFloor = 1e-9f;
    float att_ = 0.0f;
    float mean_ = 0.0f;
    bool primed_ = false;
    float ratio_ = 1.0f;
    float attRatio_ = 1.0f;
};

// TouchDesigner's Trigger: fires once when x rises past `on`, then re-arms only after x falls below half
// of it (hysteresis) and `refractory` seconds have passed (re-trigger delay). Returns the strength, 0..1.
class Trigger {
public:
    float step(float x, float on, float refractory, float dt) {
        since_ += dt;
        if (!armed_ && x < on * kRelease) armed_ = true;
        if (armed_ && x > on && since_ >= refractory) {
            armed_ = false;
            since_ = 0.0f;
            return std::clamp(x / (2.0f * on), 0.05f, 1.0f);
        }
        return 0.0f;
    }
    void reset() { *this = Trigger(); }

private:
    static constexpr float kRelease = 0.5f;
    bool armed_ = true;
    float since_ = 1e9f;
};

// The envelope a trigger drives: rises to the hit's strength over `attack`, then decays with `decay`
// (sustain 0, release = decay). A new hit while still ringing rises from where it is.
class Envelope {
public:
    float step(float hit, float attack, float decay, float dt) {
        if (hit > 0.0f) {
            target_ = std::max(hit, value_);
            rising_ = true;
        }
        if (rising_) {
            value_ += target_ * dt / attack;
            if (value_ >= target_) {
                value_ = target_;
                rising_ = false;
            }
        } else {
            value_ *= std::exp(-dt / decay);
        }
        return value_;
    }
    void reset() { *this = Envelope(); }

private:
    float value_ = 0.0f;
    float target_ = 0.0f;
    bool rising_ = false;
};

// MilkDrop's hard cut, kept as a trigger: fires when the mean relative level passes a threshold T, then
// doubles T so the next one needs an even bigger moment. T relaxes back toward its resting level, the
// excess shrinking fourfold per `halfLife` seconds (an effective half-life of halfLife / 2).
class HardCut {
public:
    float step(float meanRatio, float dt) {
        if (meanRatio > threshold_) {
            threshold_ *= 2.0f;
            return 1.0f;
        }
        threshold_ = kRest + (threshold_ - kRest) * std::exp(-std::log(4.0f) * dt / kHalfLife);
        return 0.0f;
    }
    void reset() { threshold_ = 2.0f * kRest; }

private:
    static constexpr float kRest = 2.5f;
    static constexpr float kHalfLife = 60.0f;
    float threshold_ = 2.0f * kRest;
};

}  // namespace geode::sound
