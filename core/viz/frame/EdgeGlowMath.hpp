#pragma once

#include <algorithm>
#include <cmath>

#include "sound/SoundFrame.h"

// The edge glow's pure maths (EdgeGlowPass, #439): its options, the sound envelope the band breathes with,
// the band and corner geometry, and the mask the shader draws, mirrored here so core/tests/edge_glow_test.cpp
// pins it without GL. Header-only and GL-free on purpose: the pass includes it for prepare(), the test for
// everything, and neither has to link the other's world.
//
// The look (the wallpaper's WallpaperLook.EDGE): the visual stays along the screen's rounded rim and fades
// to real black inward, so on an OLED panel most of the screen is off and icons and the lock clock sit on
// black instead of on a busy picture. The band is the only part that moves: it widens with the music's level,
// attack fast and release slow, the way a VU meter moves, so it pulses on the beat. The colour is always
// black (only the blend weight varies); there is no colour option, as there was none in the Kotlin it
// replaces (wallpaper/EdgeGlow.kt before #439).
namespace geode::viz::frame::edge_glow {

// The options as they cross the ABI (geode_viz_set_edge_glow, GEODE_EDGE_GLOW_FLOATS), in this order:
//   0 enabled (> 0.5 = on)
//   1, 2 the window's left and top, 3, 4 its width and height: the part of the surface on screen, in
//        surface pixels, y DOWN (as Android reports the launcher's pan). A width or height <= 0 means the
//        whole surface.
// Why a window: a wallpaper surface can be wider than the display so the launcher can pan it, and the edge
// belongs on the screen's edge, not on the surface's, which is off screen on the middle pages.
inline constexpr int kFloats = 5;

struct Options {
    bool enabled = false;
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Too few floats, or any non-finite one, turns the glow off: a garbled call from the shell shows the full
// visual rather than a black screen.
inline Options parse(const float* opts, int count) {
    Options o;
    if (opts == nullptr || count < kFloats) return o;
    for (int i = 0; i < kFloats; ++i) {
        if (!std::isfinite(opts[i])) return o;
    }
    o.enabled = opts[0] > 0.5f;
    o.left = opts[1];
    o.top = opts[2];
    o.width = opts[3];
    o.height = opts[4];
    return o;
}

// The band at rest and at full level, as fractions of the window's short side.
inline constexpr float kBandMin = 0.045f;
inline constexpr float kBandSwing = 0.12f;
// Close to a modern phone's display corners; the black outside them hides any small mismatch.
inline constexpr float kCornerFraction = 0.09f;
// Window RMS (linear full scale) to a 0..1 level: a mastered track's ~0.2 RMS reads about 0.7.
inline constexpr float kLevelGain = 3.5f;
// Per-frame rates at kRefFps, the wallpaper's paced rate the look was tuned at; envelope() rescales them by
// dt so a 30 fps cap or a 120 Hz dream breathes at the same speed.
inline constexpr float kRefFps = 60.0f;
inline constexpr float kAttack = 0.5f;
inline constexpr float kRelease = 0.08f;
// While nothing new is heard (paused, between captures) the level eases off by this much a reference frame.
inline constexpr float kHoldDecay = 0.9f;
// A frame longer than this (a stall, the first frame) steps the envelope as if it were this long.
inline constexpr float kMaxDt = 0.25f;

// A per-reference-frame rate k as the share of the gap closed over dt seconds.
inline float rateOver(float k, float dt) {
    const float frames = std::clamp(std::isfinite(dt) ? dt : 0.0f, 0.0f, kMaxDt) * kRefFps;
    return 1.0f - std::pow(1.0f - k, frames);
}

// The level the band reaches for: the frame's RMS through the gain, 0..1. Silence (or no sound) is 0.
inline float targetLevel(const GeodeSoundFrame* sound) {
    if (sound == nullptr || !std::isfinite(sound->rms)) return 0.0f;
    return std::clamp(sound->rms * kLevelGain, 0.0f, 1.0f);
}

// The VU-meter envelope. `fresh` is false when the sound frame carries nothing new since the last frame
// (its RMS repeats bit for bit, which a playing signal never does); then the target is the level itself,
// decayed, so a paused song lets the band settle instead of holding it wide.
struct Envelope {
    float level = 0.0f;
    float lastRms = -1.0f;

    void reset() { *this = Envelope{}; }

    float step(const GeodeSoundFrame* sound, float dt) {
        const float rms = (sound != nullptr && std::isfinite(sound->rms)) ? sound->rms : 0.0f;
        const bool fresh = sound != nullptr && rms != lastRms;
        lastRms = rms;
        float target = 0.0f;
        if (fresh) {
            target = targetLevel(sound);
        } else if (sound != nullptr) {
            target = level * (1.0f - rateOver(1.0f - kHoldDecay, dt));
        }
        level += (target - level) * rateOver(target > level ? kAttack : kRelease, dt);
        level = std::clamp(level, 0.0f, 1.0f);
        return level;
    }
};

// The window in GL pixels, y UP (gl_FragCoord's frame): x, y of its bottom-left corner, width, height.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

inline Rect glWindow(const Options& o, int surfaceWidth, int surfaceHeight) {
    const float sw = static_cast<float>(std::max(surfaceWidth, 1));
    const float sh = static_cast<float>(std::max(surfaceHeight, 1));
    if (o.width <= 0.0f || o.height <= 0.0f) return {0.0f, 0.0f, sw, sh};
    return {o.left, sh - o.top - o.height, o.width, o.height};
}

// What the shader is given this frame.
struct Shape {
    Rect window;
    float bandPx = 0.0f;
    float radiusPx = 0.0f;
};

inline Shape shape(const Rect& window, float level) {
    const float shortSide = std::max(std::min(window.w, window.h), 1.0f);
    Shape s;
    s.window = window;
    s.bandPx = shortSide * (kBandMin + kBandSwing * std::clamp(level, 0.0f, 1.0f));
    s.radiusPx = shortSide * kCornerFraction;
    return s;
}

inline float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The black's weight at GL pixel (px, py): 0 (clear) on the rim, 1 a band's width inside it and beyond,
// and 1 outside the rounded corners. The same expression as edge_glow_frag.glsl; keep the two in step.
inline float blackAt(const Shape& s, float px, float py) {
    const float hx = s.window.w * 0.5f;
    const float hy = s.window.h * 0.5f;
    const float x = px - (s.window.x + hx);
    const float y = py - (s.window.y + hy);
    const float qx = std::abs(x) - (hx - s.radiusPx);
    const float qy = std::abs(y) - (hy - s.radiusPx);
    const float outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
    const float d = outside + std::min(std::max(qx, qy), 0.0f) - s.radiusPx;
    if (d > 0.0f) return 1.0f;
    const float inside = smoothstep(0.0f, s.bandPx, -d);
    return inside * inside * (3.0f - 2.0f * inside);
}

}  // namespace geode::viz::frame::edge_glow
