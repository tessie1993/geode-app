#pragma once
#include <cstdint>
#include <optional>

// The artwork palette: a cover's dominant hue and how far its colours spread, read from its pixels. Customize ›
// Look's "Take the colours from the artwork" lays it on the palette slot, and the widget's glow takes the cover's
// hue from it. Ported from the Kotlin `dev.geode.analysis.ArtPalette` it replaces, in single-precision floats in
// the same order, so the old results hold (art_palette_test pins them). Pure, any thread.
namespace geode::viz::art {

inline constexpr int kBins = 36;

struct Extracted {
    float baseHue = 0.0f;     // 0..1, the strongest hue bin's centre, nudged towards its heavier neighbour
    float span = 0.0f;        // 0.06..1 of a turn, the reach of the bins that carry real weight
    float confidence = 0.0f;  // share of the pixels that counted as coloured
};

// Hue, saturation and value of one 0xAARRGGBB pixel (alpha ignored), each 0..1, hue wrapped into [0, 1).
void hsv(int32_t argb, float out[3]);

// Nothing for no pixels, or when none is coloured: too grey, too dark or blown out.
std::optional<Extracted> extract(const int32_t* argb, int n);

}  // namespace geode::viz::art
