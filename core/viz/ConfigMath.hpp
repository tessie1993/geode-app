#pragma once
#include <cmath>

#include "viz/Config.hpp"

namespace geode::viz::config {

// The conversions the data stores run on old saves and the palette maker's preview hue, as pure
// functions over the rows in viz/ConfigTable.inc (#423). Each is the Kotlin formula it replaces, to the
// float, on every finite input; core/tests/config_schema_test.cpp holds them to sample values. Kotlin
// still runs its own copies until the shell calls these (the ABI does not carry them yet).

// LfoStore.readRateSeconds (LfoStore.kt:150-155): a slot saved by an older build held a frequency
// (`rateHz`); the period it described keeps the slot's speed. No rate, zero or a negative one reads as
// the default period. A NaN also reads as the default, where Kotlin's `hz <= 0f` let it through as NaN.
inline float lfoRateSecondsFromHz(float hz) {
    if (!(hz > 0.0f)) return spec(Slot::lfo_rateSeconds).defaultValue;
    return clampTo(Slot::lfo_rateSeconds, 1.0f / hz);
}

// PaletteStore.sampleHue (PaletteStore.kt:108-117): stop `index` of `stops` along a gradient from
// baseHue spanning hueSpan of a turn, wrapped into 0..1. Kotlin's Float `%` is fmod's truncated
// remainder, so the double wrap is the same. PREVIEW_STOPS (PaletteStore.kt:60) is the preview's count.
inline constexpr int kPalettePreviewStops = 9;

inline float paletteSampleHue(float baseHue, float hueSpan, int index, int stops = kPalettePreviewStops) {
    const float t = stops <= 1 ? 0.0f : static_cast<float>(index) / static_cast<float>(stops - 1);
    const float h = baseHue + hueSpan * t;
    return std::fmod(std::fmod(h, 1.0f) + 1.0f, 1.0f);
}

}  // namespace geode::viz::config
