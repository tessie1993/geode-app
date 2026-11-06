#pragma once

#include <algorithm>
#include <cmath>

// The live wallpaper's two small decisions, once Kotlin in wallpaper/VisualizerWallpaperService (#605):
// which part of its surface is on screen (the window the edge glow draws its rim on, EdgeGlowMath.hpp), and
// the frame rate its pacer asks for. Header-only, GL-free and stateless: the service hands in the raw
// values Android gave it and keeps nothing but the answer; core/tests/wallpaper_window_test.cpp pins both.
namespace geode::viz::frame::wallpaper {

// The raw WallpaperService.Engine.onOffsetsChanged values as they cross the ABI (geode_wallpaper_window,
// GEODE_WALLPAPER_OFFSETS), in the callback's own order:
//   0, 1 xOffset, yOffset: where the screen sits on the surface, 0 (left / top) .. 1 (right / bottom)
//   2, 3 xOffsetStep, yOffsetStep: the launcher's page step (0.5 = three pages); 0, -1 or NaN when it never said
//   4, 5 xPixels, yPixels: the same position as the surface's offset from the screen, in pixels (<= 0)
inline constexpr int kOffsetFloats = 6;

// A surface this much bigger than the display is a panning one: within a nav bar's worth of the display,
// the surface is the screen.
inline constexpr float kPanSlack = 1.1f;

struct Offsets {
    float x = NAN;
    float y = NAN;
    float xStep = NAN;
    float yStep = NAN;
    float xPixels = 0.0f;
    float yPixels = 0.0f;
};

// Missing floats stay unknown: an offset NaN falls back to the pixels, missing pixels to 0 (no pan).
inline Offsets parseOffsets(const float* raw, int count) {
    Offsets o;
    if (raw == nullptr) return o;
    if (count > 0) o.x = raw[0];
    if (count > 1) o.y = raw[1];
    if (count > 2) o.xStep = raw[2];
    if (count > 3) o.yStep = raw[3];
    if (count > 4) o.xPixels = raw[4];
    if (count > 5) o.yPixels = raw[5];
    return o;
}

// The part of the surface on screen, in surface pixels, y down: what geode_viz_set_edge_glow's window takes.
struct Window {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

namespace detail {

inline float positive(float v) { return std::isfinite(v) && v > 0.0f ? v : 0.0f; }

// One axis: where the screen starts on the surface, and how long it is.
//
// The offset is the authority: it is what the launcher set (WallpaperManager.setWallpaperOffsets, 0..1) and
// what the window manager itself turns into the surface's position, offset × (surface − display), so it is
// used at full precision rather than through xPixels' rounding. The pixels are the fallback for an offset
// that is not a number. The step plays no part: it only says how many pages the launcher has, and a launcher
// that never set it (0, -1, NaN) still pans by its offsets. Either way the pan is clamped to the surface, so
// an overscrolling launcher (an offset past 0..1, or a positive xPixels) never puts the rim off the surface.
inline void axis(float surface, float display, float offset, float pixels, float& start, float& length) {
    start = 0.0f;
    length = surface;
    if (display <= 0.0f || surface <= display * kPanSlack) return;
    const float range = surface - display;
    float pan = 0.0f;
    if (std::isfinite(offset)) {
        pan = std::clamp(offset, 0.0f, 1.0f) * range;
    } else if (std::isfinite(pixels)) {
        pan = std::clamp(-pixels, 0.0f, range);
    }
    start = pan;
    length = display;
}

}  // namespace detail

// The window for a surface of surfaceW × surfaceH on a display of displayW × displayH. A surface no bigger
// than the display (within kPanSlack) on an axis does not pan there: the window is the whole surface on
// that axis. A size that is not a positive number is treated as unknown: an unknown surface gives a zero
// window, which the edge glow reads as the whole surface; an unknown display never pans.
inline Window window(float surfaceW, float surfaceH, float displayW, float displayH, const Offsets& o) {
    const float sw = detail::positive(surfaceW);
    const float sh = detail::positive(surfaceH);
    const float dw = detail::positive(displayW);
    const float dh = detail::positive(displayH);
    Window w;
    detail::axis(sw, dw, o.x, o.xPixels, w.left, w.width);
    detail::axis(sh, dh, o.y, o.yPixels, w.top, w.height);
    return w;
}

// The wallpaper's frame rate: its own target, lowered to Settings › Performance's cap. A cap that is not a
// positive number is no cap; a target that is not one falls back to the cap, then to kDefaultFps, so the
// pacer is never handed 0 or NaN.
inline constexpr float kDefaultFps = 60.0f;

inline float frameRate(float target, float cap) {
    const float t = detail::positive(target);
    const float c = detail::positive(cap);
    if (t > 0.0f && c > 0.0f) return std::min(t, c);
    if (t > 0.0f) return t;
    if (c > 0.0f) return c;
    return kDefaultFps;
}

}  // namespace geode::viz::frame::wallpaper
