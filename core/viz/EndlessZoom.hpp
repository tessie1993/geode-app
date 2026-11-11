#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "sound/DriveGain.hpp"
#include "sound/SoundFrame.h"

namespace geode::viz::endless {

// Endless zoom: travel through the picture that never arrives. Falling in, driving through, rising out.
//
// It is one stage of its own (viz/DivePass, dive_frag.glsl), drawn over the finished picture of whatever
// style is on screen: a flat shader, a marched 3D scene, MilkDrop, a simulation, particles. The style
// does not know about it, and nothing else moves it: not the style's Zoom, the camera, Trails or the
// reactions. Its only inputs are the switch and Dive speed (signed: in falls, out rises).
//
// The picture is nested inside itself an octave apart (the Droste effect). Every copy grows at Dive
// speed, a new one opens at the centre, the outermost passes the frame and is gone, so the pair of
// frames at phase 1 is the pair at phase 0 and the loop has no seam. The copies are this frame's picture:
// nothing is fed back, so the style stays live and sharp at any speed.
//
// What it replaced, and why each failed (git log -- core/viz/EndlessZoom.hpp):
//  - one magnification 2^phase popped at the wrap; its triangle fix zoomed in and back out;
//  - two copies an octave apart, crossfaded: the scale only ran 1x..2x, a pulse, not travel;
//  - MilkDrop's feedback zoom: the live style was poured over the smear at under 1% a frame and born in
//    a disc a seventh of the frame wide, so the screen showed seconds-old blur magnified 7x, the style's
//    own motion gone; and the bass, mids and treble pushed its zoom, turn and warp on every hit.

/** Dive speed's reach either way, octaves a second (the schema's range). */
inline constexpr float kMaxSpeed = 1.2f;

/**
 * One frame of travel: phase in octaves, wrapped into [0, 1). A negative speed backs out just as
 * seamlessly; non-finite input holds the phase rather than poisoning it.
 */
inline float advance(float phase, float octavesPerSecond, float dt) {
    const float step = octavesPerSecond * dt;
    if (!std::isfinite(step) || !std::isfinite(phase)) return std::isfinite(phase) ? phase : 0.0f;
    const float next = phase + step;
    const float wrapped = next - std::floor(next);
    return wrapped >= 1.0f ? 0.0f : wrapped;  // -1e-9 floors to 1.0 in float
}

// The music's hand on the dive is slow or nothing. No hit, band or beat reaches the rate: Follow reads
// only how big the music is (the absolute drive, core/sound/DriveGain) averaged over the last kBeats
// beats the sound engine measured, and moves the rate by at most kReach either way. Without a locked
// beat there is no window to average over, so it lets go and the dive runs at exactly Dive speed.

class Follow {
public:
    /** Measured beats in the average. */
    static constexpr int kBeats = 8;
    /** The most the music moves the rate, either way. */
    static constexpr float kReach = 0.15f;
    /** Rhythm's lock (SoundFrame.h: beatConf). */
    static constexpr float kLocked = 0.35f;
    /** Seconds the multiplier takes to ease to a new average, and back to 1 when the beat is lost. */
    static constexpr float kEaseSeconds = 2.0f;

    /** Once a frame, dt its length in seconds; audioDrive (0..2) scales the reach, 0 switches it off. */
    void step(const GeodeSoundFrame& s, float dt, float audioDrive) {
        dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f;
        const float conf = std::isfinite(s.beatConf) ? s.beatConf : 0.0f;
        const float phase = std::isfinite(s.beatPhase) ? s.beatPhase : 0.0f;
        float target = 1.0f;
        if (conf < kLocked) {
            count_ = 0;
            sum_ = time_ = 0.0f;
        } else {
            // A beat is the tracker's phase wrapping, whatever the frame rate.
            if (phase < lastPhase_ - 0.5f && time_ > 0.0f) {
                beats_[static_cast<size_t>(next_)] = sum_ / time_;
                next_ = (next_ + 1) % kBeats;
                count_ = std::min(count_ + 1, kBeats);
                sum_ = time_ = 0.0f;
            }
            sum_ += (std::isfinite(s.drive) ? std::clamp(s.drive, 0.0f, 1.0f) : 0.0f) * dt;
            time_ += dt;
            if (count_ == kBeats) {
                float mean = 0.0f;
                for (float b : beats_) mean += b;
                mean /= static_cast<float>(kBeats);
                const float reach = kReach * (std::isfinite(audioDrive) ? std::clamp(audioDrive, 0.0f, 1.0f) : 1.0f);
                target = 1.0f + reach * (2.0f * std::clamp(mean / sound::kDriveFull, 0.0f, 1.0f) - 1.0f);
            }
        }
        lastPhase_ = phase;
        rate_ += (target - rate_) * (1.0f - std::exp(-dt / kEaseSeconds));
    }

    /** The multiplier on Dive speed, 1 - kReach .. 1 + kReach. */
    float rate() const { return rate_; }

private:
    std::array<float, kBeats> beats_{};
    int next_ = 0;
    int count_ = 0;
    float sum_ = 0.0f;
    float time_ = 0.0f;
    float lastPhase_ = 0.0f;
    float rate_ = 1.0f;
};

/** Octaves a second the dive travels: Dive speed (signed) times the slow follow. Silence is the frame clock's. */
inline float travel(float speed, const Follow& follow) {
    if (!std::isfinite(speed)) return 0.0f;
    return std::clamp(speed, -kMaxSpeed, kMaxSpeed) * follow.rate();
}

// The nested copies, as dive_frag.glsl draws them: change both together.
namespace tunnel {

/** Copies inside the frame-filling one, and outside it (under the rounded corners). */
inline constexpr int kInner = 5;
inline constexpr int kBase = 1;
/** A copy's soft edge, in its own half-frames. */
inline constexpr float kFeather = 0.22f;
/** How dark the wall just outside a copy's edge falls, and how far out, in its own half-frames. */
inline constexpr float kShade = 0.45f;
inline constexpr float kShadeReach = 0.5f;

inline float smooth(float lo, float hi, float x) {
    const float t = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/** Copy k's magnification at `phase`: k = 0 fills the frame at phase 0, each next one is half the size. */
inline float scale(float phase, int k) { return std::exp2(phase - static_cast<float>(k)); }

/** The rounded frame's norm at (x, y), -1..1 across the frame: 1 on a copy's edge. */
inline float rim(float x, float y) { return std::sqrt(std::sqrt(x * x * x * x + y * y * y * y)); }

/** How much of copy k covers the screen point (x, y), before the copies inside it. */
inline float cover(float phase, int k, float x, float y) {
    if (k == -kBase) return 1.0f;
    const float s = scale(phase, k);
    float a = 1.0f - smooth(1.0f, 1.0f + kFeather, rim(x / s, y / s));
    if (k == kInner) a *= smooth(0.0f, 1.0f, phase);
    return a;
}

/** The shadow copy k throws on what lies outside its edge at (x, y). */
inline float shade(float phase, int k, float x, float y) {
    const float s = scale(phase, k);
    const float d = rim(x / s, y / s);
    if (d < 1.0f) return 0.0f;
    float wall = 1.0f - smooth(1.0f, 1.0f + kShadeReach, d);
    if (k == kInner) wall *= smooth(0.0f, 1.0f, phase);
    return kShade * wall;
}

/** The share of the pixel at (x, y) drawn at magnification `mag`, innermost copy first. */
inline float share(float phase, float mag, float x, float y) {
    float left = 1.0f;
    float sum = 0.0f;
    for (int k = kInner; k >= -kBase; k--) {
        const float a = cover(phase, k, x, y);
        if (std::fabs(scale(phase, k) - mag) < 1e-4f * mag) sum += left * a;
        left *= (1.0f - a) * (1.0f - shade(phase, k, x, y));
    }
    return sum;
}

}  // namespace tunnel

// Sustain: Trails on a shader style. Those styles redraw the whole frame, so a trail kept in the
// scene target was painted over and the four trail controls did nothing there. In the feedback pass
// (viz/liquid/LiquidPass) they are MilkDrop's own three: Trail length is its sustain (how long a frame
// lingers), Trail zoom its video echo and Trail warp its warp (liquid::motion::trailBase).
namespace feedback {

inline float clampedDt(float dt) { return std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f; }

/** Half-life of a lingering frame at Trail length 0 and at 1, seconds. */
inline constexpr float kShortTrail = 0.12f;
inline constexpr float kLongTrail = 3.0f;

/** The share of the style poured over the lingering frames: a long trail pours little. */
inline float sustainInject(float trailLength, float dt) {
    const float l = std::isfinite(trailLength) ? std::clamp(trailLength, 0.0f, 1.0f) : 0.5f;
    const float halfLife = kShortTrail * std::pow(kLongTrail / kShortTrail, l);
    return 1.0f - std::exp2(-clampedDt(dt) / halfLife);
}

}  // namespace feedback

}  // namespace geode::viz::endless
