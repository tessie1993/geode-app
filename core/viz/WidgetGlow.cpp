#include "viz/WidgetGlow.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace geode::viz {

namespace {

struct Seed {
    double x, y;
    float radius;
    double speed, offset;
    float saturation, value;
};

constexpr Seed kSeeds[kWidgetLights] = {
    {0.25, 0.35, 0.55f, 1.0, 0.0, 0.75f, 0.95f},
    {0.70, 0.60, 0.50f, -1.0, 2.1, 0.65f, 0.85f},
    {0.50, 0.15, 0.35f, 2.0, 4.2, 0.40f, 1.0f},
};

constexpr double kDrift = 0.12;
constexpr double kTwoPi = 6.283185307179586;
constexpr int kHueSteps = 360;
constexpr float kMinSpread = 0.04f;
constexpr float kMaxSpread = 0.18f;
constexpr float kDefaultSpread = 0.10f;
// The theme's accent hue (#9CC7FF), for a widget that has neither a cover nor a style.
constexpr float kFallbackHue = 0.59f;

float baseHue(const WidgetGlowIn& in) {
    if (in.artHue >= 0.0f) return in.artHue;
    if (in.hasStyle) return static_cast<float>(std::abs(in.styleHash % kHueSteps)) / static_cast<float>(kHueSteps);
    return kFallbackHue;
}

// Kotlin's Float.mod(1f): the result takes the divisor's sign.
float wrapUnit(float v) { return std::fmod(std::fmod(v, 1.0f) + 1.0f, 1.0f); }

}  // namespace

void widgetGlow(const WidgetGlowIn& in, WidgetLight out[kWidgetLights]) {
    const float base = baseHue(in);
    const float spread = in.artHue >= 0.0f ? std::clamp(in.artSpan, kMinSpread, kMaxSpread) : kDefaultSpread;
    const double phase = (in.tick % kWidgetTicksPerTurn) * (kTwoPi / kWidgetTicksPerTurn);
    for (int i = 0; i < kWidgetLights; ++i) {
        const Seed& s = kSeeds[i];
        const double angle = phase * s.speed + s.offset;
        out[i].x = static_cast<float>(s.x + kDrift * std::cos(angle));
        out[i].y = static_cast<float>(s.y + kDrift * std::sin(angle));
        out[i].radius = s.radius;
        out[i].hueDegrees = wrapUnit(base + spread * static_cast<float>(i - 1)) * 360.0f;
        out[i].saturation = s.saturation;
        out[i].value = s.value;
    }
}

}  // namespace geode::viz
