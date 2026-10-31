#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// OKLab (Björn Ottosson, 2020; https://bottosson.github.io/posts/oklab/, public domain): the colour space
// the colour engine thinks in. Its lightness is perceptual, so a hue can turn with the key while the
// picture's brightness stays put, and a gradient between two stops never sags into grey or drifts
// purple the way RGB, HSV and CIELAB blends do.
namespace geode::viz::colour {

using Rgb = std::array<float, 3>;  // display sRGB, 0..1 (what the shaders work in)
using Lab = std::array<float, 3>;  // OKLab: L 0..1, a and b about ±0.4

inline float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float linearToSrgb(float c) {
    c = std::max(c, 0.0f);
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline Lab linearToOklab(float r, float g, float b) {
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s, 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

// Linear sRGB, unclamped: a component outside 0..1 means the colour is outside the sRGB gamut.
inline std::array<float, 3> oklabToLinear(const Lab& c) {
    const float l = c[0] + 0.3963377774f * c[1] + 0.2158037573f * c[2];
    const float m = c[0] - 0.1055613458f * c[1] - 0.0638541728f * c[2];
    const float s = c[0] - 0.0894841775f * c[1] - 1.2914855480f * c[2];
    const float l3 = l * l * l;
    const float m3 = m * m * m;
    const float s3 = s * s * s;
    return {4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3, -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3,
            -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3};
}

inline Lab srgbToOklab(const Rgb& c) { return linearToOklab(srgbToLinear(c[0]), srgbToLinear(c[1]), srgbToLinear(c[2])); }

inline bool inGamut(const std::array<float, 3>& lin) {
    constexpr float kSlack = 1e-4f;
    return lin[0] >= -kSlack && lin[0] <= 1.0f + kSlack && lin[1] >= -kSlack && lin[1] <= 1.0f + kSlack && lin[2] >= -kSlack &&
           lin[2] <= 1.0f + kSlack;
}

// OKLab to display sRGB, keeping lightness and hue and giving up chroma until the colour fits the gamut
// (a bisection on the chroma scale). Turning a saturated stop's hue can leave sRGB, and clipping each
// channel instead would shift its hue and flatten it.
inline Rgb oklabToSrgb(Lab c) {
    c[0] = std::clamp(c[0], 0.0f, 1.0f);
    auto lin = oklabToLinear(c);
    if (!inGamut(lin)) {
        float lo = 0.0f;
        float hi = 1.0f;
        for (int i = 0; i < 10; i++) {
            const float mid = 0.5f * (lo + hi);
            if (inGamut(oklabToLinear({c[0], c[1] * mid, c[2] * mid}))) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        lin = oklabToLinear({c[0], c[1] * lo, c[2] * lo});
    }
    return {std::clamp(linearToSrgb(lin[0]), 0.0f, 1.0f), std::clamp(linearToSrgb(lin[1]), 0.0f, 1.0f), std::clamp(linearToSrgb(lin[2]), 0.0f, 1.0f)};
}

inline Rgb hexToRgb(uint32_t hex) {
    return {static_cast<float>((hex >> 16) & 0xFF) / 255.0f, static_cast<float>((hex >> 8) & 0xFF) / 255.0f, static_cast<float>(hex & 0xFF) / 255.0f};
}

// The hue of an OKLab colour turned by `degrees`, lightness and chroma kept.
inline Lab rotateHue(const Lab& c, float degrees) {
    const float r = degrees * 3.14159265f / 180.0f;
    const float cs = std::cos(r);
    const float sn = std::sin(r);
    return {c[0], c[1] * cs - c[2] * sn, c[1] * sn + c[2] * cs};
}

inline Lab mixLab(const Lab& a, const Lab& b, float t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

}  // namespace geode::viz::colour
