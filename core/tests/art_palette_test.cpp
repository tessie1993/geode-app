// Host-side checks for the cover-art palette (core/viz/ArtPalette, geode_art_palette). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no device.
//
// Pins what the Kotlin dev.geode.analysis.ArtPalette did before the port (ADR 0003): greys, near-blacks and
// blown highlights carry no hue, a single colour gives its bin's centre at the minimum span, a heavier
// neighbour bin pulls the base hue toward it across the 0/1 seam, the span reaches the farthest bin above
// 60 % of the mean weight, ties go to the lower bin, and confidence is the share of pixels that counted.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "api/geode_api.h"
#include "viz/ArtPalette.hpp"

using namespace geode::viz::art_palette;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// The C ABI as JNI calls it; false when no colour.
bool palette(const std::vector<uint32_t>& px, float out[3]) {
    return geode_art_palette(px.data(), static_cast<int>(px.size()), out) == 1;
}

std::vector<uint32_t> fill(uint32_t argb, int n) { return std::vector<uint32_t>(static_cast<std::size_t>(n), argb); }

void append(std::vector<uint32_t>& v, uint32_t argb, int n) { v.insert(v.end(), static_cast<std::size_t>(n), argb); }

constexpr float kBin = 1.0f / kBins;
constexpr uint32_t kRed = 0xFFCC0000u;     // h 0, s 1, v 0.8: bin 0, weight 0.8
constexpr uint32_t kOrange = 0xFFCCBB00u;  // h ~0.1528: bin 5, weight 0.8
constexpr uint32_t kRose = 0xFFCC0011u;    // h ~0.9861: bin 35, weight 0.8
constexpr uint32_t kGreen = 0xFF00CC00u;   // h 1/3: bin 12, weight 0.8

void nothingColoured() {
    float out[3] = {-1.0f, -1.0f, -1.0f};
    check(!palette({}, out), "no pixels: no palette");
    check(geode_art_palette(nullptr, 4, out) == 0, "null pixels: no palette");
    check(!palette(fill(0xFF808080u, 64), out), "mid grey: no palette");
    check(!palette(fill(0xFF000000u, 64), out), "black: no palette");
    check(!palette(fill(0xFFFFFFFFu, 64), out), "white: no palette");
    check(!palette(fill(0xFF100000u, 64), out), "near-black red (v < 0.12): no palette");
    check(!palette(fill(0xFFFF0000u, 64), out), "full-value red (v > 0.97): skipped as a clip, as in Kotlin");
    check(!palette(fill(0xFFCCB4B4u, 64), out), "washed-out pink (s < 0.18): no palette");
    check(out[0] == -1.0f && out[1] == -1.0f && out[2] == -1.0f, "out untouched when there is no palette");
}

void singleColour() {
    float out[3];
    check(palette(fill(kRed, 100), out), "red has a palette");
    check(near(out[0], 0.5f * kBin), "red: base hue is bin 0's centre");
    check(near(out[1], kMinSpan), "red: one bin spans the minimum");
    check(near(out[2], 1.0f), "red: every pixel counted");

    check(palette(fill(kGreen, 10), out), "green has a palette");
    check(near(out[0], 12.5f * kBin), "green: base hue is bin 12's centre");

    // Alpha is ignored, as Kotlin read only the RGB bytes.
    check(palette(fill(kGreen & 0x00FFFFFFu, 10), out) && near(out[0], 12.5f * kBin), "alpha ignored");
}

void confidenceIsShareColoured() {
    std::vector<uint32_t> px = fill(kRed, 50);
    append(px, 0xFF808080u, 50);
    float out[3];
    check(palette(px, out), "half red, half grey has a palette");
    check(near(out[2], 0.5f), "confidence is the coloured share");
    check(near(out[0], 0.5f * kBin), "greys do not move the hue");
}

void spanReachesFarBins() {
    // Red weighs 16, orange (five bins round) 8; the floor is 24 / 36 * 0.6 = 0.4, so both count.
    std::vector<uint32_t> px = fill(kRed, 20);
    append(px, kOrange, 10);
    float out[3];
    check(palette(px, out), "red and orange have a palette");
    check(near(out[0], 0.5f * kBin), "peak stays red, its neighbours are empty");
    check(near(out[1], 11.0f * kBin), "span reaches five bins either side");
}

void neighbourAcrossTheSeam() {
    // Peak bin 0 (16), its left neighbour is bin 35 (8): shift = -8 / 24 * 0.5 = -1/6 of a bin.
    std::vector<uint32_t> px = fill(kRed, 20);
    append(px, kRose, 10);
    float out[3];
    check(palette(px, out), "red and rose have a palette");
    check(near(out[0], (0.5f - 1.0f / 6.0f) * kBin), "base hue leans toward bin 35 across the seam");
    check(near(out[1], 3.0f * kBin), "span covers the peak and its wrapped neighbour");

    // Rose heavier: peak bin 35, its right neighbour wraps to bin 0. shift = 8 / 32 * 0.5 = 0.125.
    px = fill(kRose, 30);
    append(px, kRed, 10);
    check(palette(px, out), "rose-heavy has a palette");
    check(near(out[0], (35.5f + 0.125f) * kBin), "base hue leans toward bin 0 across the seam");
}

void ties() {
    // Equal weights: the first bin wins, as Kotlin's maxByOrNull.
    std::vector<uint32_t> px = fill(kGreen, 10);
    append(px, kRed, 10);
    float out[3];
    check(palette(px, out), "tie has a palette");
    check(near(out[0], 0.5f * kBin), "a tie goes to the lower bin");
}

void wrapAndHsv() {
    check(wrap01(1.0f) == 0.0f, "wrap01(1) = 0");
    check(near(wrap01(-0.25f), 0.75f), "wrap01 of a negative wraps up");
    check(wrap01(3e-8f) == 0.0f, "wrap01 snaps a hair above 0 to 0");
    const Hsv c = hsv(kRose);
    check(near(c.h, 1.0f - 17.0f / 204.0f / 6.0f), "rose's hue wraps below 1");
    check(near(c.s, 1.0f) && near(c.v, 0.8f), "rose's saturation and value");
}

}  // namespace

int main() {
    nothingColoured();
    singleColour();
    confidenceIsShareColoured();
    spanReachesFarBins();
    neighbourAcrossTheSeam();
    ties();
    wrapAndHsv();
    if (failures) {
        std::printf("%d art palette check(s) failed\n", failures);
        return 1;
    }
    std::printf("art palette: all checks passed\n");
    return 0;
}
