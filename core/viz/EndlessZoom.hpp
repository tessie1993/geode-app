#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "sound/DriveGain.hpp"
#include "sound/SoundFrame.h"
#include "viz/Params.hpp"

namespace geode::viz::endless {

// Endless zoom: travel through the picture that never arrives. Falling in, driving through, rising out.
//
// It is one stage of its own (viz/DivePass, dive_frag.glsl), drawn over the finished picture of whatever
// style is on screen: a flat shader, a marched 3D scene, MilkDrop, a simulation, particles. The style
// does not know about it, and nothing else moves it: not the style's Zoom, the camera, Trails or the
// reactions. Its inputs are the switch, Dive speed (signed: in falls, out rises) and the Dive controls.
//
// The picture is nested inside itself (the Droste effect), a copy every Ring spacing octaves. Every copy
// grows at Dive speed, a new one opens at the vanishing point, the outermost passes the frame and is
// gone, so the frame at phase 1 is the frame at phase 0 and the loop has no seam. The copies are this
// frame's picture: nothing is fed back, so the style stays live and sharp at any speed. The Dive
// category shapes the tunnel (tunnel::Look): spacing, twist, aim, outline, blend, depth fade, lens.
//
// What it replaced, and why each failed (git log -- core/viz/EndlessZoom.hpp):
//  - one magnification 2^phase popped at the wrap; its triangle fix zoomed in and back out;
//  - two copies an octave apart, crossfaded: the scale only ran 1x..2x, a pulse, not travel;
//  - MilkDrop's feedback zoom: the live style was poured over the smear at under 1% a frame and born in
//    a disc a seventh of the frame wide, so the screen showed seconds-old blur magnified 7x, the style's
//    own motion gone; and the bass, mids and treble pushed its zoom, turn and warp on every hit;
//  - copies with a feathered rounded frame, a shadow thrown outside each edge and a mirrored border
//    under the feather: every copy read as a ridge, a ring of dark and a kink travelling outward.

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

    /**
     * Once a frame, dt its length in seconds. audioDrive (0..2) and Music follow (0..2, 1 as designed)
     * scale the reach; 0 in either switches it off.
     */
    void step(const GeodeSoundFrame& s, float dt, float audioDrive, float follow = 1.0f) {
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
                const float reach = kReach * (std::isfinite(audioDrive) ? std::clamp(audioDrive, 0.0f, 1.0f) : 1.0f) *
                                    (std::isfinite(follow) ? std::clamp(follow, 0.0f, 2.0f) : 1.0f);
                target = 1.0f + reach * (2.0f * std::clamp(mean / sound::kDriveFull, 0.0f, 1.0f) - 1.0f);
            }
        }
        lastPhase_ = phase;
        rate_ += (target - rate_) * (1.0f - std::exp(-dt / kEaseSeconds));
    }

    /** The multiplier on Dive speed, 1 - kReach .. 1 + kReach at Music follow 1. */
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

/** The same in copies a second, what the phase counts: closer rings pass more often at one speed. */
inline float copiesPerSecond(float octavesPerSecond, float spacing) {
    return octavesPerSecond / std::clamp(std::isfinite(spacing) ? spacing : 1.0f, 0.5f, 2.0f);
}

// The nested copies, as dive_frag.glsl draws them: change both together.
//
// No ridges (the owner's word for what the first tunnel drew): a copy has no outline, no shadow and no
// mirrored border. It dissolves into the copy around it across its own band, inside its edge, so a
// flat picture stays exactly flat at every phase and no line travels with the dive.
namespace tunnel {

/** How far in the copies reach, in octaves. How far out is what covers the frame (lookFor). */
inline constexpr float kInnerOctaves = 5.0f;
/** The vanishing point's reach from the centre, of the half frame. */
inline constexpr float kMaxAim = 0.6f;
/** The hardest edge Blend 0 draws, as a share of the copy: an anti-aliased cut, the user's choice. */
inline constexpr float kMinFade = 0.004f;
inline constexpr float kQuarterTurn = 1.5707964f;

/** The Dive controls as the stage reads them (dive_frag's uniforms). */
struct Look {
    float step = 1.0f;     // octaves between two copies
    int inner = 5;
    int base = 1;
    float twist = 0.0f;    // radians a copy turns per octave it grows
    float cx = 0.0f;       // the vanishing point, -1..1 across the frame
    float cy = 0.0f;
    float shape = 4.0f;    // the outline's exponent: 2 round .. 8 nearly square
    float blend = 0.75f;   // the dissolve, as a share of the copy's band
    float fog = 0.0f;      // the far end's darkening, as an exponent
    float lens = 0.0f;     // how much more (or less) of the picture a copy's band holds
    float aspect = 1.0f;   // width / height
};

inline float finite(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

inline Look lookFor(const SceneParams& p, float aspect) {
    Look l;
    l.step = std::clamp(finite(p.diveSpacing, 1.0f), 0.5f, 2.0f);
    l.inner = static_cast<int>(std::ceil(kInnerOctaves / l.step));
    l.twist = std::clamp(finite(p.diveTwist, 0.0f), -1.0f, 1.0f) * kQuarterTurn;
    l.cx = std::clamp(finite(p.diveAimX, 0.0f), -kMaxAim, kMaxAim);
    l.cy = std::clamp(finite(p.diveAimY, 0.0f), -kMaxAim, kMaxAim);
    l.shape = std::exp2(1.0f + 2.0f * std::clamp(finite(p.diveShape, 0.5f), 0.0f, 1.0f));
    l.blend = std::clamp(finite(p.diveBlend, 0.75f), 0.0f, 1.0f);
    l.fog = 1.5f * std::clamp(finite(p.diveFog, 0.0f), 0.0f, 1.0f);
    l.lens = std::clamp(finite(p.diveLens, 0.0f), -0.5f, 1.5f);
    l.aspect = std::clamp(finite(aspect, 1.0f), 0.1f, 10.0f);
    // Out as far as it takes for the copy before the last to cover the whole frame, dissolve and all,
    // when the phase wraps: the last copy then takes its place unseen. A frame corner is 2^(1/shape)
    // out in the outline's norm; a twisted copy can turn its near side to the frame's far corner.
    float reach = std::exp2(1.0f / l.shape);
    if (l.twist != 0.0f) {
        const float far = std::hypot((1.0f + std::fabs(l.cx)) * l.aspect, 1.0f + std::fabs(l.cy));
        reach = std::max(reach, far / std::min((1.0f - std::fabs(l.cx)) * l.aspect, 1.0f - std::fabs(l.cy)));
    }
    const float fade = std::max(l.blend * (1.0f - std::exp2(-l.step)), kMinFade);
    l.base = std::max(1, static_cast<int>(std::ceil(std::log2(reach * 1.02f / (1.0f - fade)) / l.step)));
    return l;
}

inline float smooth(float lo, float hi, float x) {
    const float t = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/** Octaves copy k has grown at `phase` (in copies): k = 0 fills the frame at phase 0. */
inline float octaves(const Look& l, float phase, int k) { return (phase - static_cast<float>(k)) * l.step; }
inline float scale(const Look& l, float phase, int k) { return std::exp2(octaves(l, phase, k)); }

/** The outline's norm at (x, y), -1..1 across the picture: 0 at the vanishing point, 1 on a copy's edge. */
inline float rim(const Look& l, float x, float y) {
    const float ex = (x - l.cx) / (x >= l.cx ? 1.0f - l.cx : 1.0f + l.cx);
    const float ey = (y - l.cy) / (y >= l.cy ? 1.0f - l.cy : 1.0f + l.cy);
    return std::pow(std::pow(std::fabs(ex) + 1e-6f, l.shape) + std::pow(std::fabs(ey) + 1e-6f, l.shape), 1.0f / l.shape);
}

/** Where on copy k's picture the screen point (x, y) falls, -1..1. */
inline void into(const Look& l, float phase, int k, float x, float y, float& qx, float& qy) {
    const float o = octaves(l, phase, k);
    const float s = std::exp2(o);
    const float c = std::cos(l.twist * o);
    const float n = std::sin(l.twist * o);
    const float vx = (x - l.cx) * l.aspect;
    const float vy = y - l.cy;
    qx = l.cx + (c * vx + n * vy) / (l.aspect * s);
    qy = l.cy + (c * vy - n * vx) / s;
}

/** How much of copy k covers the screen point (x, y), before the copies inside it. */
inline float cover(const Look& l, float phase, int k, float x, float y) {
    if (k == -l.base) return 1.0f;
    float qx = 0.0f;
    float qy = 0.0f;
    into(l, phase, k, x, y, qx, qy);
    const float fade = std::max(l.blend * (1.0f - std::exp2(-l.step)), kMinFade);
    float a = 1.0f - smooth(1.0f - fade, 1.0f, rim(l, qx, qy));
    if (k == l.inner) a *= smooth(0.0f, 1.0f, phase);
    return a;
}

/** Copy k's share of the pixel at (x, y), innermost copy first. */
inline float share(const Look& l, float phase, int k, float x, float y) {
    float left = 1.0f;
    for (int j = l.inner; j > k; j--) left *= 1.0f - cover(l, phase, j, x, y);
    return left * cover(l, phase, k, x, y);
}

/** What the stage multiplies the pixel at (x, y) by: the far end's fade, 1 everywhere at Depth fade 0. */
inline float light(const Look& l, float x, float y) {
    if (l.fog <= 0.0f) return 1.0f;
    return std::pow(std::clamp(rim(l, x, y), 0.0f, 1.0f), l.fog);
}

/** Texels a copy shrunk to `s` spreads its four taps over: 0 at full size, growing without a step. */
inline float footprint(float s) { return s >= 1.0f ? 0.0f : std::min(1.0f / s - 1.0f, 12.0f); }

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
