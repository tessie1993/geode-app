#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// The cover art's palette: the hue the artwork leans to, how wide its colours spread around that hue,
// and how much of the picture is coloured at all. Settings' "take the palette from the artwork" and the
// now-playing widget's glow tint the visuals with it (PaletteStore.applyGradient on the Kotlin side).
//
// A per-pixel hue histogram of 36 bins, each pixel weighted by saturation × value. Greys, near-blacks
// and blown-out highlights carry no hue a viewer would name, so they are skipped (and so is a fully
// saturated colour at full value, which on covers is almost always clipping). The base hue is the
// heaviest bin, nudged toward its heavier neighbour by a three-bin parabola-like shift; the span reaches
// as far round the wheel from it as any bin still holds 60 % of the mean weight.
//
// Ported from the Kotlin dev.geode.analysis.ArtPalette (ADR 0003): same float maths, same order of
// operations, so a cover gives the palette it gave before. Pure, no allocation: the shell hands in the
// Bitmap's getPixels() output (packed 0xAARRGGBB, alpha ignored) from a background thread.
namespace geode::viz::art_palette {

inline constexpr int kBins = 36;
inline constexpr float kMinSaturation = 0.18f;
inline constexpr float kMinValue = 0.12f;
inline constexpr float kMaxValue = 0.97f;
inline constexpr float kMinSpan = 0.06f;

struct Palette {
    float baseHue = 0.0f;     // 0..1
    float span = 0.0f;        // kMinSpan..1, the fraction of the hue wheel
    float confidence = 0.0f;  // 0..1, the share of pixels that counted
};

// v mod 1 in [0, 1), with 1 and a hair above 0 snapped to 0.
inline float wrap01(float v) {
    const float x = v - std::floor(v);
    return (x >= 1.0f || std::fabs(x) < 1e-7f) ? 0.0f : x;
}

struct Hsv {
    float h, s, v;
};

inline Hsv hsv(uint32_t argb) {
    const float r = static_cast<float>((argb >> 16) & 0xFFu) / 255.0f;
    const float g = static_cast<float>((argb >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>(argb & 0xFFu) / 255.0f;
    const float hi = std::fmax(r, std::fmax(g, b));
    const float lo = std::fmin(r, std::fmin(g, b));
    const float d = hi - lo;
    float h;
    if (d < 1e-6f) {
        h = 0.0f;
    } else if (hi == r) {
        h = (g - b) / d / 6.0f;
    } else if (hi == g) {
        h = ((b - r) / d + 2.0f) / 6.0f;
    } else {
        h = ((r - g) / d + 4.0f) / 6.0f;
    }
    const float s = hi <= 0.0f ? 0.0f : d / hi;
    return {wrap01(h), s, hi};
}

// The palette of count packed ARGB pixels; false (out untouched) when none of them has a colour.
inline bool extract(const uint32_t* argb, int count, Palette& out) {
    if (!argb || count <= 0) return false;
    std::array<float, kBins> weights{};
    int coloured = 0;
    for (int i = 0; i < count; ++i) {
        const Hsv c = hsv(argb[i]);
        if (c.s < kMinSaturation || c.v < kMinValue || c.v > kMaxValue) continue;
        ++coloured;
        int bin = static_cast<int>(c.h * static_cast<float>(kBins));
        bin = bin < 0 ? 0 : (bin > kBins - 1 ? kBins - 1 : bin);
        weights[static_cast<std::size_t>(bin)] += c.s * c.v;
    }
    if (coloured == 0) return false;
    int peak = -1;
    float total = 0.0f;
    for (int i = 0; i < kBins; ++i) {
        const float w = weights[static_cast<std::size_t>(i)];
        total += w;
        if (w > 0.0f && (peak < 0 || w > weights[static_cast<std::size_t>(peak)])) peak = i;
    }
    if (peak < 0) return false;

    auto at = [&](int i) { return weights[static_cast<std::size_t>(((i % kBins) + kBins) % kBins)]; };
    const float left = at(peak - 1);
    const float right = at(peak + 1);
    const float denom = left + at(peak) + right;
    const float shift = denom > 0.0f ? (right - left) / denom * 0.5f : 0.0f;
    const float baseHue = wrap01((static_cast<float>(peak) + 0.5f + shift) / static_cast<float>(kBins));

    const float floorWeight = total / static_cast<float>(kBins) * 0.6f;
    int reach = 0;
    for (int step = 1; step <= kBins / 2; ++step) {
        if (at(peak + step) >= floorWeight || at(peak - step) >= floorWeight) reach = step;
    }
    const float span = std::fmax(kMinSpan, (static_cast<float>(reach) * 2.0f + 1.0f) / static_cast<float>(kBins));

    out.baseHue = baseHue;
    out.span = std::fmin(span, 1.0f);
    out.confidence = static_cast<float>(coloured) / static_cast<float>(count);
    return true;
}

}  // namespace geode::viz::art_palette
