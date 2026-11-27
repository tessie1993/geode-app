#pragma once

#include <algorithm>
#include <cmath>

#include "sound/DriveGain.hpp"
#include "sound/SoundFrame.h"
#include "viz/modulation/VisualSafety.hpp"
#include "viz/liquid/LiquidMath.hpp"

namespace geode::viz::liquid::motion {

// The feedback motion: every frame the last frame is re-sampled through one UV transform (zoom, zoomexp,
// stretch, warp, rotation about a centre, translation) and decayed, so a small per-frame value compounds
// into travel. This is that transform as pure maths, for the feedback pass to upload as uniforms.
//
// Per-frame values would move a picture twice as fast at twice the frame rate. Here a Motion is per second
// and perFrame() turns it into one frame's values for the frame's dt, so the same travel per second results
// at any frame rate.
//
// app/src/main/assets/shaders/lib_feedback.glsl is uvFor() and fold() in GLSL, term for term: change
// both together.

inline constexpr float kFrameHz = 60.0f;
/** The warp displacement per frame per unit warp, as a rate at 60 Hz. */
inline constexpr float kWarpPerSecond = 0.0035f * kFrameHz;

/** Per-second motion; the defaults are the identity. */
struct Motion {
    float zoom = 1.0f;       // magnification per second about the centre; above 1 dives in, below backs out
    float zoomExp = 1.0f;    // radial profile: the zoom at radius r is zoom^(zoomExp^(2r - 1))
    float rot = 0.0f;        // radians per second about (cx, cy)
    float cx = 0.5f;         // centre of stretch and rotation in the aspect-fitted UV, v down
    float cy = 0.5f;
    float dx = 0.0f;         // UV per second; positive moves the picture right
    float dy = 0.0f;         // UV per second; positive moves the picture down
    float sx = 1.0f;         // stretch per second about (cx, cy)
    float sy = 1.0f;
    float warp = 0.0f;       // the warp amount; 0 is still
    float warpScale = 1.0f;  // spatial scale of the warp: larger is broader
    float warpSpeed = 1.0f;  // the warp animation speed: how fast the warp's pattern drifts
};

/** One frame's values, in per-frame units: what the feedback shader takes as uniforms. */
struct FrameMotion {
    float zoom = 1.0f;
    float zoomExp = 1.0f;
    float rot = 0.0f;
    float cx = 0.5f;
    float cy = 0.5f;
    float dx = 0.0f;
    float dy = 0.0f;
    float sx = 1.0f;
    float sy = 1.0f;
    float warp = 0.0f;  // displacement amplitude this frame, UV
    float warpScale = 1.0f;
    float warpSpeed = 1.0f;
};

struct Uv {
    float u = 0.5f;
    float v = 0.5f;
};

// Hard limits on what any caller can ask for, per second.
inline constexpr float kMaxZoomInOctaves = 2.0f;
inline constexpr float kMaxZoomOutOctaves = 1.0f;
inline constexpr float kMaxZoomExp = 4.0f;
inline constexpr float kMaxRot = 1.5f;
inline constexpr float kMaxShift = 0.5f;
inline constexpr float kMaxStretchOctaves = 1.0f;
inline constexpr float kMaxWarp = 4.0f;
inline constexpr float kMaxDt = 0.1f;

namespace detail {
inline float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }
inline float octaves(float factor, float maxIn, float maxOut) {
    if (!std::isfinite(factor) || factor <= 0.0f) return 0.0f;
    return std::clamp(std::log2(factor), -maxOut, maxIn);
}
}  // namespace detail

/** Every field finite and inside the limits; anything non-finite falls back to the identity's value. */
inline Motion sanitize(const Motion& m) {
    using detail::finiteOr;
    Motion o;
    o.zoom = std::exp2(detail::octaves(m.zoom, kMaxZoomInOctaves, kMaxZoomOutOctaves));
    o.zoomExp = std::clamp(finiteOr(m.zoomExp, 1.0f), 1.0f / kMaxZoomExp, kMaxZoomExp);
    o.rot = std::clamp(finiteOr(m.rot, 0.0f), -kMaxRot, kMaxRot);
    o.cx = std::clamp(finiteOr(m.cx, 0.5f), -1.0f, 2.0f);
    o.cy = std::clamp(finiteOr(m.cy, 0.5f), -1.0f, 2.0f);
    o.dx = std::clamp(finiteOr(m.dx, 0.0f), -kMaxShift, kMaxShift);
    o.dy = std::clamp(finiteOr(m.dy, 0.0f), -kMaxShift, kMaxShift);
    o.sx = std::exp2(detail::octaves(m.sx, kMaxStretchOctaves, kMaxStretchOctaves));
    o.sy = std::exp2(detail::octaves(m.sy, kMaxStretchOctaves, kMaxStretchOctaves));
    o.warp = std::clamp(finiteOr(m.warp, 0.0f), 0.0f, kMaxWarp);
    o.warpScale = std::clamp(finiteOr(m.warpScale, 1.0f), 0.05f, 20.0f);
    o.warpSpeed = std::clamp(finiteOr(m.warpSpeed, 1.0f), 0.0f, 10.0f);
    return o;
}

/** This frame's share of a per-second Motion: rates times dt, factors to the power dt. */
inline FrameMotion perFrame(const Motion& motion, float dt) {
    const Motion m = sanitize(motion);
    const float t = std::isfinite(dt) ? std::clamp(dt, 0.0f, kMaxDt) : 0.0f;
    FrameMotion f;
    f.zoom = std::pow(m.zoom, t);
    f.zoomExp = m.zoomExp;
    f.rot = m.rot * t;
    f.cx = m.cx;
    f.cy = m.cy;
    f.dx = m.dx * t;
    f.dy = m.dy * t;
    f.sx = std::pow(m.sx, t);
    f.sy = std::pow(m.sy, t);
    f.warp = m.warp * kWarpPerSecond * t;
    f.warpScale = m.warpScale;
    f.warpSpeed = m.warpSpeed;
    return f;
}

// Which of uvFor's stages a frame runs (FrameTerms::flags). A stage whose values are the identity's is
// skipped, and skipping it is exact: the stage would add 0 or multiply by 1.
inline constexpr int kMoves = 1;       // anything but the identity; without it the sample is where it draws
inline constexpr int kRadialZoom = 2;  // zoomExp is not 1, so the zoom varies with the radius
inline constexpr int kWarps = 4;       // warp is not 0

/**
 * uvFor's terms that depend only on the frame's motion and the clock, derived once a frame on the CPU:
 * what lib_feedback reads as uniforms, so no pixel works them out again.
 */
struct FrameTerms {
    int flags = 0;
    float zoom = 1.0f;
    float zoomExp = 1.0f;
    float cx = 0.5f;
    float cy = 0.5f;
    float dx = 0.0f;
    float dy = 0.0f;
    float sx = 1.0f;
    float sy = 1.0f;
    float rotCos = 1.0f;
    float rotSin = 0.0f;
    float warp = 0.0f;
    float warpInvScale = 1.0f;  // 1 / warpScale
    float warpFreq[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // the warp's four drifting frequencies at this time
    float warpPhase[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // each warp term's phase at this time
};

/** `f`'s terms at `time` (seconds, the clock the warp drifts on). */
inline FrameTerms derive(const FrameMotion& f, float time) {
    FrameTerms t;
    t.zoom = f.zoom;
    t.zoomExp = f.zoomExp;
    t.cx = f.cx;
    t.cy = f.cy;
    t.dx = f.dx;
    t.dy = f.dy;
    t.sx = f.sx;
    t.sy = f.sy;
    t.rotCos = std::cos(f.rot);
    t.rotSin = std::sin(f.rot);
    t.warp = f.warp;
    t.warpInvScale = 1.0f / f.warpScale;
    const float wt = (std::isfinite(time) ? time : 0.0f) * f.warpSpeed;
    t.warpFreq[0] = 11.68f + 4.0f * std::cos(wt * 1.413f + 10.0f);
    t.warpFreq[1] = 8.77f + 3.0f * std::cos(wt * 1.113f + 7.0f);
    t.warpFreq[2] = 10.54f + 3.0f * std::cos(wt * 1.233f + 3.0f);
    t.warpFreq[3] = 11.49f + 4.0f * std::cos(wt * 0.933f + 5.0f);
    t.warpPhase[0] = wt * 0.333f;
    t.warpPhase[1] = wt * 0.375f;
    t.warpPhase[2] = wt * 0.753f;
    t.warpPhase[3] = wt * 0.825f;
    // A NaN fails every comparison, so a broken value runs its stage and uvFor's own guard catches it.
    const bool identity = f.zoom == 1.0f && f.sx == 1.0f && f.sy == 1.0f && f.rot == 0.0f && f.dx == 0.0f &&
                          f.dy == 0.0f && f.warp == 0.0f;
    if (!identity) t.flags |= kMoves;
    if (!(f.zoomExp == 1.0f)) t.flags |= kRadialZoom;
    if (!(f.warp == 0.0f)) t.flags |= kWarps;
    return t;
}

/**
 * The UV of the last frame a destination pixel samples. `uv` is GL's (v up), `aspect` is width / height.
 * The zoom (its exponent shaping it by radius) about the centre, the stretch, the warp, the rotation, the
 * translation and the undo of the aspect fit are applied in that order. The mesh's v runs down, so v is
 * flipped in and out. The result is unfolded: fold() it before sampling.
 */
inline Uv uvFor(const FrameTerms& t, Uv uv, float aspect) {
    if (!std::isfinite(uv.u) || !std::isfinite(uv.v)) return {};
    if (!(t.flags & kMoves)) return uv;
    const float a = (std::isfinite(aspect) && aspect > 0.0f) ? aspect : 1.0f;
    const float aspX = a < 1.0f ? a : 1.0f;
    const float aspY = a > 1.0f ? 1.0f / a : 1.0f;
    const float x = uv.u * 2.0f - 1.0f;
    const float y = uv.v * 2.0f - 1.0f;  // the mesh y: 1 - 2 v with v down, which is 2 v - 1 with v up

    // zoom^(1^r) is zoom whatever the radius.
    float zoom2 = t.zoom;
    if (t.flags & kRadialZoom) {
        const float rad = std::sqrt(x * x * aspX * aspX + y * y * aspY * aspY);
        zoom2 = std::pow(t.zoom, std::pow(t.zoomExp, rad * 2.0f - 1.0f));
    }
    const float zoomInv = 1.0f / zoom2;
    float u = x * aspX * 0.5f * zoomInv + 0.5f;
    float v = -y * aspY * 0.5f * zoomInv + 0.5f;

    u = (u - t.cx) / t.sx + t.cx;
    v = (v - t.cy) / t.sy + t.cy;

    if (t.flags & kWarps) {
        const float si = t.warpInvScale;
        const float* fq = t.warpFreq;
        const float* ph = t.warpPhase;
        u += t.warp * std::sin(ph[0] + si * (x * fq[0] - y * fq[3]));
        v += t.warp * std::cos(ph[1] - si * (x * fq[2] + y * fq[1]));
        u += t.warp * std::cos(ph[2] - si * (x * fq[1] - y * fq[2]));
        v += t.warp * std::sin(ph[3] + si * (x * fq[0] + y * fq[3]));
    }

    const float u2 = u - t.cx;
    const float v2 = v - t.cy;
    u = u2 * t.rotCos - v2 * t.rotSin + t.cx;
    v = u2 * t.rotSin + v2 * t.rotCos + t.cy;

    u -= t.dx;
    v -= t.dy;

    u = (u - 0.5f) / aspX + 0.5f;
    v = (v - 0.5f) / aspY + 0.5f;
    if (!std::isfinite(u) || !std::isfinite(v)) return uv;
    return {u, 1.0f - v};
}

/** uvFor at `time`: the frame's terms derived, then applied. */
inline Uv uvFor(const FrameMotion& f, Uv uv, float aspect, float time) { return uvFor(derive(f, time), uv, aspect); }

/** Mirror fold into 0..1 (`1 - |1 - mod(p, 2)|`): an edge pulled inward reflects the picture, never smears it. */
inline float fold(float p) {
    if (!std::isfinite(p)) return 0.5f;
    const float m = p - 2.0f * std::floor(p * 0.5f);
    return 1.0f - std::fabs(1.0f - m);
}
inline Uv fold(Uv p) { return {fold(p.u), fold(p.v)}; }

// The music's hand on the motion, the idiom `zoom = base + k (bass_att - 1)`: each band's att is
// its level over its own running mean, 1 typical, so the motion surges on the hits and eases between
// them and averages back to the base. Bass zooms, mids turn, treble boils the warp.

/** Octaves a second of zoom per unit of bassAtt above its mean. */
inline constexpr float kBassZoom = 0.5f;
/** Radians a second of rotation per unit of midAtt above its mean. */
inline constexpr float kMidRot = 0.3f;
/** Warp per unit of trebleAtt above its mean. */
inline constexpr float kTrebleWarp = 1.0f;
/** How far a band's att may sit from its mean before the push stops growing. */
inline constexpr float kMaxSwing = 2.0f;

/**
 * `base` (the user's and the style's motion) pushed by the sound frame. The push is scaled by the
 * absolute drive (sound::driveGain); everything that moves, base included, by sound::motionRate, so
 * silence is the identity (owner's rule: no sound, no movement). Under reduced motion the push takes
 * VisualSafety's scale; the base arrives already scaled by safety::apply. Shape (zoomExp, centre,
 * warp scale and speed) passes through.
 */
inline Motion fromSound(const Motion& base, const GeodeSoundFrame& s, bool reducedMotion = false) {
    const Motion b = sanitize(base);
    const float rate = sound::motionRate(s);
    const float reach = sound::driveGain(s) * (reducedMotion ? safety::kReducedMotionScale : 1.0f);
    auto swing = [](float att) { return std::clamp(detail::finiteOr(att, 1.0f) - 1.0f, -1.0f, kMaxSwing); };

    Motion m = b;
    const float zoomOct = std::log2(b.zoom) + kBassZoom * swing(s.bassAtt) * reach;
    m.zoom = std::exp2(std::clamp(zoomOct, -kMaxZoomOutOctaves, kMaxZoomInOctaves) * rate);
    m.rot = std::clamp(b.rot + kMidRot * swing(s.midAtt) * reach, -kMaxRot, kMaxRot) * rate;
    m.warp = std::clamp(b.warp + kTrebleWarp * swing(s.trebleAtt) * reach, 0.0f, kMaxWarp) * rate;
    m.dx = b.dx * rate;
    m.dy = b.dy * rate;
    m.sx = std::exp2(std::log2(b.sx) * rate);
    m.sy = std::exp2(std::log2(b.sy) * rate);
    return m;
}

// What the renderer asks of the pass while Liquid is off (Renderer::drawTrails): a shader style's Trails
// as an echo zoom and warp. Endless zoom is not here: it is its own stage (viz/passes/DivePass).
struct TrailAsk {
    float trailZoom = 0.0f;  // -0.5..0.5
    float trailWarp = 0.0f;  // 0..1
};

/** Trail zoom at 1, octaves a second: the 6% a frame the particle styles' trail pass uses, at 60 Hz. */
inline constexpr float kTrailZoomOctaves = 5.2f;

inline Motion trailBase(const TrailAsk& a) {
    using detail::finiteOr;
    Motion m;
    m.zoom = std::exp2(std::clamp(finiteOr(a.trailZoom, 0.0f), -0.5f, 0.5f) * kTrailZoomOctaves);
    m.warp = std::clamp(finiteOr(a.trailWarp, 0.0f), 0.0f, 1.0f);
    return sanitize(m);
}

/** This frame's motion for the ask: the user's own zoom and warp, held to the silence rule; no band pushes it. */
inline Motion trailMotion(const TrailAsk& a, const GeodeSoundFrame& s, bool reducedMotion) {
    GeodeSoundFrame heard = s;
    heard.bassAtt = heard.midAtt = heard.trebleAtt = 1.0f;
    return fromSound(trailBase(a), heard, reducedMotion);
}

/**
 * Liquid mode's dive (LiquidMath's Dive: a zoom and a twist about a wandering vanishing point) as one
 * frame of the feedback transform. It zooms about the screen's centre and turns about (cx, cy), so
 * a zoom about the vanishing point is that zoom, the turn about it, and the translation that carries the
 * centre's zoom over to it: d = R (0.5 - c) (1 - 1 / zoom). The vanishing point comes in centred units
 * (x times the aspect, y up), which are the aspect-fitted UV over 0.5 aspY, y flipped.
 */
inline FrameMotion fromDive(const DiveFrame& d, float aspect) {
    using detail::finiteOr;
    const float a = (std::isfinite(aspect) && aspect > 0.0f) ? aspect : 1.0f;
    const float k = 0.5f * (a > 1.0f ? 1.0f / a : 1.0f);
    FrameMotion f;
    f.zoom = std::clamp(finiteOr(d.scale, 1.0f), 0.5f, 2.0f);
    f.rot = std::clamp(finiteOr(d.twist, 0.0f), -kMaxRot, kMaxRot);
    f.cx = 0.5f + std::clamp(finiteOr(d.vpX, 0.0f), -2.0f, 2.0f) * k;
    f.cy = 0.5f - std::clamp(finiteOr(d.vpY, 0.0f), -2.0f, 2.0f) * k;
    const float pull = 1.0f - 1.0f / f.zoom;
    const float ox = (0.5f - f.cx) * pull;
    const float oy = (0.5f - f.cy) * pull;
    const float c = std::cos(f.rot);
    const float s = std::sin(f.rot);
    f.dx = ox * c - oy * s;
    f.dy = ox * s + oy * c;
    return f;
}

}  // namespace geode::viz::liquid::motion
