#pragma once

#include <algorithm>
#include <cmath>

#include "sound/DriveGain.hpp"
#include "sound/SoundFrame.h"
#include "viz/VisualSafety.hpp"
#include "viz/liquid/LiquidMath.hpp"

namespace geode::viz::liquid::motion {

// MilkDrop's motion: every frame the last frame is re-sampled through one UV transform (zoom, zoomexp,
// stretch, warp, rotation about a centre, translation) and decayed, so a small per-frame value compounds
// into travel. This is that transform as pure maths, for the feedback pass to upload as uniforms.
//
// MilkDrop applies its values once per frame, so a preset moves twice as fast at 120 Hz as at 60. Here a
// Motion is per second and perFrame() turns it into one frame's MilkDrop values for the frame's dt, so the
// same travel per second results at any frame rate.
//
// app/src/main/assets/shaders/lib_feedback.glsl is uvFor() and fold() in GLSL, term for term: change
// both together.

inline constexpr float kMilkDropHz = 60.0f;
/** MilkDrop's warp displacement per frame per unit warp (milkdropfs.cpp:1812), as a rate at its 60 Hz. */
inline constexpr float kWarpPerSecond = 0.0035f * kMilkDropHz;

/** Per-second motion with MilkDrop's names; the defaults are the identity. */
struct Motion {
    float zoom = 1.0f;       // magnification per second about the centre; above 1 dives in, below backs out
    float zoomExp = 1.0f;    // radial profile: the zoom at radius r is zoom^(zoomExp^(2r - 1))
    float rot = 0.0f;        // radians per second about (cx, cy)
    float cx = 0.5f;         // centre of stretch and rotation in MilkDrop's aspect-fitted UV, v down
    float cy = 0.5f;
    float dx = 0.0f;         // UV per second; positive moves the picture right
    float dy = 0.0f;         // UV per second; positive moves the picture down
    float sx = 1.0f;         // stretch per second about (cx, cy)
    float sy = 1.0f;
    float warp = 0.0f;       // MilkDrop's warp amount (its preset default is 1); 0 is still
    float warpScale = 1.0f;  // spatial scale of the warp: larger is broader
    float warpSpeed = 1.0f;  // MilkDrop's warp anim speed: how fast the warp's pattern drifts
};

/** One frame's values, MilkDrop's per-frame units: what the feedback shader takes as uniforms. */
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

/** A MilkDrop preset's per-frame values (at `hz`, normally its 60) as a per-second Motion. */
inline Motion fromMilkDrop(const FrameMotion& f, float hz = kMilkDropHz) {
    Motion m;
    m.zoom = std::pow(f.zoom, hz);
    m.zoomExp = f.zoomExp;
    m.rot = f.rot * hz;
    m.cx = f.cx;
    m.cy = f.cy;
    m.dx = f.dx * hz;
    m.dy = f.dy * hz;
    m.sx = std::pow(f.sx, hz);
    m.sy = std::pow(f.sy, hz);
    m.warp = f.warp * hz / kMilkDropHz;
    m.warpScale = f.warpScale;
    m.warpSpeed = f.warpSpeed;
    return sanitize(m);
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

/**
 * The UV of the last frame a destination pixel samples. `uv` is GL's (v up), `aspect` is width / height,
 * `time` the clock the warp drifts on (seconds). MilkDrop3 milkdropfs.cpp:1699-1705 (warp time and its
 * four drifting frequencies), 1794 (zoomexp), 1797-1799 (zoom about the centre), 1806-1807 (stretch),
 * 1812-1815 (warp), 1819-1825 (rotation), 1828-1829 (translation), 1832-1833 (undo the aspect fit);
 * aspect and rad as plugin.cpp:2035-2038 and 2289. MilkDrop's v runs down, so v is flipped in and out;
 * its half-texel offset is D3D9's and is left out. The result is unfolded: fold() it before sampling.
 */
inline Uv uvFor(const FrameMotion& f, Uv uv, float aspect, float time) {
    if (!std::isfinite(uv.u) || !std::isfinite(uv.v)) return {};
    const float a = (std::isfinite(aspect) && aspect > 0.0f) ? aspect : 1.0f;
    const float aspX = a < 1.0f ? a : 1.0f;
    const float aspY = a > 1.0f ? 1.0f / a : 1.0f;
    const float x = uv.u * 2.0f - 1.0f;
    const float y = uv.v * 2.0f - 1.0f;  // MilkDrop's mesh y: 1 - 2 v with v down, which is 2 v - 1 with v up

    const float rad = std::sqrt(x * x * aspX * aspX + y * y * aspY * aspY);
    const float zoom2 = std::pow(f.zoom, std::pow(f.zoomExp, rad * 2.0f - 1.0f));
    const float zoomInv = 1.0f / zoom2;
    float u = x * aspX * 0.5f * zoomInv + 0.5f;
    float v = -y * aspY * 0.5f * zoomInv + 0.5f;

    u = (u - f.cx) / f.sx + f.cx;
    v = (v - f.cy) / f.sy + f.cy;

    const float wt = (std::isfinite(time) ? time : 0.0f) * f.warpSpeed;
    const float si = 1.0f / f.warpScale;
    const float f0 = 11.68f + 4.0f * std::cos(wt * 1.413f + 10.0f);
    const float f1 = 8.77f + 3.0f * std::cos(wt * 1.113f + 7.0f);
    const float f2 = 10.54f + 3.0f * std::cos(wt * 1.233f + 3.0f);
    const float f3 = 11.49f + 4.0f * std::cos(wt * 0.933f + 5.0f);
    u += f.warp * std::sin(wt * 0.333f + si * (x * f0 - y * f3));
    v += f.warp * std::cos(wt * 0.375f - si * (x * f2 + y * f1));
    u += f.warp * std::cos(wt * 0.753f - si * (x * f1 - y * f2));
    v += f.warp * std::sin(wt * 0.825f + si * (x * f0 + y * f3));

    const float u2 = u - f.cx;
    const float v2 = v - f.cy;
    const float c = std::cos(f.rot);
    const float s = std::sin(f.rot);
    u = u2 * c - v2 * s + f.cx;
    v = u2 * s + v2 * c + f.cy;

    u -= f.dx;
    v -= f.dy;

    u = (u - 0.5f) / aspX + 0.5f;
    v = (v - 0.5f) / aspY + 0.5f;
    if (!std::isfinite(u) || !std::isfinite(v)) return uv;
    return {u, 1.0f - v};
}

/** Mirror fold into 0..1 (`1 - |1 - mod(p, 2)|`): an edge pulled inward reflects the picture, never smears it. */
inline float fold(float p) {
    if (!std::isfinite(p)) return 0.5f;
    const float m = p - 2.0f * std::floor(p * 0.5f);
    return 1.0f - std::fabs(1.0f - m);
}
inline Uv fold(Uv p) { return {fold(p.u), fold(p.v)}; }

// The music's hand on the motion, MilkDrop's idiom `zoom = base + k (bass_att - 1)`: each band's att is
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
// as MilkDrop's video echo zoom and warp. Endless zoom is not here: it is its own stage (viz/DivePass).
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
 * frame of MilkDrop's transform. MilkDrop zooms about the screen's centre and turns about (cx, cy), so
 * a zoom about the vanishing point is that zoom, the turn about it, and the translation that carries the
 * centre's zoom over to it: d = R (0.5 - c) (1 - 1 / zoom). The vanishing point comes in centred units
 * (x times the aspect, y up), which are MilkDrop's aspect-fitted UV over 0.5 aspY, y flipped.
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
