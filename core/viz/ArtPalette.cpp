#include "viz/ArtPalette.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace geode::viz::art {

namespace {

// Greys, near-blacks and blown highlights say nothing about a cover's colour.
constexpr float kMinSaturation = 0.18f;
constexpr float kMinValue = 0.12f;
constexpr float kMaxValue = 0.97f;

// A single hot bin still reads as a narrow gradient, not one flat colour.
constexpr float kMinSpan = 0.06f;

// Into [0, 1); a value that lands on 1 or a hair off 0 is 0, as the Kotlin did.
float wrap01(float v) {
    const float x = v - std::floor(v);
    return (x >= 1.0f || std::fabs(x) < 1e-7f) ? 0.0f : x;
}

}  // namespace

void hsv(int32_t argb, float out[3]) {
    const auto px = static_cast<uint32_t>(argb);
    const float r = static_cast<float>((px >> 16) & 0xFFu) / 255.0f;
    const float g = static_cast<float>((px >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>(px & 0xFFu) / 255.0f;
    const float hi = std::max(r, std::max(g, b));
    const float lo = std::min(r, std::min(g, b));
    const float d = hi - lo;
    float h = 0.0f;
    if (d < 1e-6f) {
        h = 0.0f;
    } else if (hi == r) {
        h = (g - b) / d / 6.0f;
    } else if (hi == g) {
        h = ((b - r) / d + 2.0f) / 6.0f;
    } else {
        h = ((r - g) / d + 4.0f) / 6.0f;
    }
    out[0] = wrap01(h);
    out[1] = hi <= 0.0f ? 0.0f : d / hi;
    out[2] = hi;
}

std::optional<Extracted> extract(const int32_t* argb, int n) {
    if (!argb || n <= 0) return std::nullopt;
    std::array<float, kBins> weights{};
    int coloured = 0;
    for (int i = 0; i < n; ++i) {
        float c[3];
        hsv(argb[i], c);
        const float s = c[1];
        const float v = c[2];
        if (s < kMinSaturation || v < kMinValue || v > kMaxValue) continue;
        ++coloured;
        const int bin = std::clamp(static_cast<int>(c[0] * static_cast<float>(kBins)), 0, kBins - 1);
        // Its own statement, so no compiler fuses it into an FMA and drifts an ulp from the Kotlin it replaces.
        const float w = s * v;
        weights[bin] += w;
    }
    const float confidence = static_cast<float>(coloured) / static_cast<float>(n);
    if (coloured == 0 || std::all_of(weights.begin(), weights.end(), [](float w) { return w <= 0.0f; })) {
        return std::nullopt;
    }

    // The first bin of the heaviest weight, as Kotlin's maxByOrNull picks it.
    int peak = 0;
    for (int i = 1; i < kBins; ++i) {
        if (weights[i] > weights[peak]) peak = i;
    }
    const float left = weights[(peak - 1 + kBins) % kBins];
    const float right = weights[(peak + 1) % kBins];
    const float denom = left + weights[peak] + right;
    const float shift = denom > 0.0f ? (right - left) / denom * 0.5f : 0.0f;
    const float baseHue = wrap01((static_cast<float>(peak) + 0.5f + shift) / static_cast<float>(kBins));

    float total = 0.0f;
    for (float w : weights) total += w;
    const float hotFloor = total / static_cast<float>(kBins) * 0.6f;
    int reach = 0;
    for (int step = 1; step <= kBins / 2; ++step) {
        const bool hot = weights[(peak + step) % kBins] >= hotFloor || weights[(peak - step + kBins) % kBins] >= hotFloor;
        if (hot) reach = step;
    }
    const float span = std::max(kMinSpan, (static_cast<float>(reach) * 2.0f + 1.0f) / static_cast<float>(kBins));
    return Extracted{baseHue, std::min(span, 1.0f), confidence};
}

}  // namespace geode::viz::art
