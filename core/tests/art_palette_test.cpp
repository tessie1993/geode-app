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
#include <random>
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

// ---- Seam: geode_art_palette (the C ABI the JNI calls). Expected values are worked by hand from the
// HSV hexcone (hue° = 60 × sextant position) and the palette's rules, never recomputed through the code.

bool coloured(uint32_t argb) {
    float out[3];
    return palette(fill(argb, 8), out);
}

// Kills: channel / 256 instead of / 255, kMaxValue moved (0.96, 0.98). 247/255 = 0.969 is a colour,
// 248/255 = 0.973 is a blown highlight (/256 would read 248 as 0.969 and keep it).
void brightestKeptRedIs247() {
    check(coloured(0xFFF70000u), "red at value 247/255 (0.969) still has a colour");
    check(!coloured(0xFFF80000u), "red at value 248/255 (0.973) is a blown highlight");
}

// Kills: kMinValue moved (0.11, 0.13), the value test reading kMinSaturation. 31/255 = 0.122 is a colour,
// 30/255 = 0.118 is near-black.
void darkestKeptRedIs31() {
    check(coloured(0xFF1F0000u), "red at value 31/255 (0.122) still has a colour");
    check(!coloured(0xFF1E0000u), "red at value 30/255 (0.118) is near-black");
}

// Kills: kMinSaturation moved (0.17, 0.19 and beyond), the saturation test reading kMinValue (0.12).
// (200, 163, 163) has s = 37/200 = 0.185; (200, 165, 165) has s = 35/200 = 0.175.
void palestKeptPinkIsSaturation0185() {
    check(coloured(0xFFC8A3A3u), "pink at saturation 0.185 has a colour");
    check(!coloured(0xFFC8A5A5u), "pink at saturation 0.175 is a grey");
}

// Kills: weight += 1 (a pixel count), += s (value dropped), += v (saturation dropped).
// 10 dim reds (s 1, v 0.4: 4.0) lose to 6 bright greens (s 1, v 0.8: 4.8), so the base hue is green's
// bin centre, 125°. 10 bright reds (8.0) beat 12 pale greens (s 0.5, v 0.8: 4.8), so it stays red, 5°.
void brightAndSaturatedPixelsOutweighMany() {
    float out[3];
    std::vector<uint32_t> px = fill(0xFF660000u, 10);
    append(px, kGreen, 6);
    check(palette(px, out) && near(out[0], 125.0f / 360.0f), "fewer but brighter greens set the hue");

    px = fill(kRed, 10);
    append(px, 0xFF66CC66u, 12);
    check(palette(px, out) && near(out[0], 5.0f / 360.0f), "more but paler greens do not set the hue");
}

// Kills: the 0.6 of the mean moved (0.5, 0.7), the mean taken over 35 bins. With one green 12 bins from
// the red peak the floor is 0.6 × total / 36: 58 reds + 1 green put it at 0.787 under green's 0.8, so the
// span reaches it (25 bins, 250°); 60 reds + 1 green put it at 0.813 over, so the span stays the minimum.
void spanReachesABinHolding60PercentOfTheMean() {
    float out[3];
    std::vector<uint32_t> px = fill(kRed, 58);
    append(px, kGreen, 1);
    check(palette(px, out) && near(out[1], 250.0f / 360.0f), "a bin over 60 % of the mean widens the span");

    px = fill(kRed, 60);
    append(px, kGreen, 1);
    check(palette(px, out) && near(out[1], kMinSpan), "a bin under 60 % of the mean does not");
}

// Kills: the reach loop stopping before half the wheel (step < 18), the span not capped at 1.
// Red (0°) and cyan (180°) are opposite: the span must cover the whole wheel, never more. Equal weight goes
// to the lower bin (Kotlin's maxByOrNull), so red's 5°; heavier cyan gives cyan's 185°.
void oppositeHuesSpanTheWholeWheel() {
    constexpr uint32_t kCyan = 0xFF00CCCCu;
    float out[3];
    std::vector<uint32_t> px = fill(kRed, 10);
    append(px, kCyan, 10);
    check(palette(px, out) && near(out[1], 1.0f), "red and cyan span the whole wheel");
    check(near(out[0], 5.0f / 360.0f), "red and cyan tied: the lower bin, red");

    px = fill(kRed, 10);
    append(px, kCyan, 12);
    check(palette(px, out) && near(out[0], 185.0f / 360.0f) && near(out[1], 1.0f),
          "heavier cyan: its bin centre, still the whole wheel");
}

// Kills: the neighbour lookup not wrapping (bin -1), the shift's sign flipped, ties going to the higher bin.
// Red (bin 0, 0-10°) and rose (bin 35, 350-360°) weighed alike: the hue stays at the 0/360 seam, the short
// way round (2.5°: the peak's 5° pulled a quarter bin toward rose), never the long way to 180°.
void evenRedAndRoseStayAtTheSeam() {
    std::vector<uint32_t> px = fill(kRed, 20);
    append(px, kRose, 20);
    float out[3];
    check(palette(px, out) && near(out[0], 2.5f / 360.0f), "red and rose meet at 2.5°, across the seam");
    check(near(out[1], 3.0f * kBin), "red and rose span three bins, not the wheel");
}

// Kills, blue-led sextant: (r - g) flipped to (g - r), its + 4 moved. Green-led sextant: (b - r) flipped.
// (51, 0, 204) is 240° + 60° × 51/204 = 255°; (0, 204, 51) is 120° + 60° × 51/204 = 135°. Each is its
// bin's centre, so the base hue is exactly that angle.
void blueAndGreenLedHuesLandOnTheWheel() {
    float out[3];
    check(palette(fill(0xFF3300CCu, 10), out) && near(out[0], 255.0f / 360.0f), "violet sits at 255°");
    check(palette(fill(0xFF00CC33u, 10), out) && near(out[0], 135.0f / 360.0f), "spring green sits at 135°");
}

// Kills: count <= 1 (one pixel treated as none), out filled without the null check. One orange pixel
// (204, 187, 0: 60° × 187/204 = 55°) is a full palette at the minimum span; out may be null.
void onePixelIsEnough() {
    const uint32_t one[1] = {kOrange};
    float out[3];
    check(geode_art_palette(one, 1, out) == 1, "one coloured pixel has a palette");
    check(near(out[0], 55.0f / 360.0f) && near(out[1], kMinSpan) && near(out[2], 1.0f),
          "one orange pixel: 55°, the minimum span, full confidence");
    check(geode_art_palette(one, 1, nullptr) == 0, "a null out is refused, not written");
}

// ---- Properties over seeded random covers. The truths are colour-wheel facts, not the code's maths:
// rotating the channels (R, G, B) -> (B, R, G) turns every hue by exactly 120°, swapping G and B mirrors
// every hue (h -> 360° - h), alpha carries no colour, and greys only dilute the confidence.

constexpr uint32_t argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) { return (a << 24) | (r << 16) | (g << 8) | b; }

uint32_t rotateChannels(uint32_t p) {  // (R, G, B) -> (B, R, G): hue + 120°
    return argb(p >> 24, p & 0xFFu, (p >> 16) & 0xFFu, (p >> 8) & 0xFFu);
}

uint32_t swapGreenBlue(uint32_t p) {  // hue -> 360° - hue
    return argb(p >> 24, (p >> 16) & 0xFFu, p & 0xFFu, (p >> 8) & 0xFFu);
}

float turnDistance(float a, float b) {  // the shorter way round, in turns
    const float d = std::fmod(std::fabs(a - b), 1.0f);
    return std::fmin(d, 1.0f - d);
}

struct Rng {
    std::mt19937 engine;  // raw output only: the sequence is fixed by the standard, distributions are not
    explicit Rng(uint32_t seed) : engine(seed) {}
    uint32_t below(uint32_t n) { return static_cast<uint32_t>(engine()) % n; }
};

// A coloured pixel with chroma d (in 1/255 steps), safely coloured (value 40..240, saturation >= 0.2) and
// with its hue between 10 % and 90 % of a 10° bin, so no float rounding can move it across a bin edge under
// rotation or mirroring. Hue position inside a 60° sextant is 6 (mid - lo) / d bins; its fraction is
// ((6 (mid - lo)) mod d) / d.
uint32_t colouredPixel(Rng& rng, uint32_t d) {
    const uint32_t loHi = d > 40u ? d : 40u;
    const uint32_t hiHi = d * 5u < 240u ? d * 5u : 240u;
    const uint32_t hi = loHi + rng.below(hiHi - loHi + 1u);
    const uint32_t lo = hi - d;
    uint32_t mid = 0;  // set by the do-while below; initialised so -Werror never sees a maybe-uninitialized read
    do {
        mid = lo + 1u + rng.below(d - 1u);
    } while ((6u * (mid - lo)) % d * 10u < d || (6u * (mid - lo)) % d * 10u > 9u * d);
    const uint32_t c[6][3] = {{hi, mid, lo}, {mid, hi, lo}, {lo, hi, mid}, {lo, mid, hi}, {mid, lo, hi}, {hi, lo, mid}};
    const uint32_t* order = c[rng.below(6u)];
    return argb(rng.below(256u), order[0], order[1], order[2]);
}

// A pixel with no colour: a grey (s = 0), a blown highlight (value 255) or a near-black (value <= 25).
uint32_t colourlessPixel(Rng& rng) {
    const uint32_t a = rng.below(256u);
    switch (rng.below(3u)) {
        case 0: {
            const uint32_t v = rng.below(256u);
            return argb(a, v, v, v);
        }
        case 1:
            return argb(a, 255u, rng.below(256u), rng.below(256u));
        default:
            return argb(a, rng.below(26u), rng.below(26u), rng.below(26u));
    }
}

// Kills, among others: any channel read from the wrong byte or from alpha, any sextant's offset or sign
// moved, the shift's sign flipped or its neighbours swapped, the neighbour lookup not wrapping, greys
// counted (|| -> &&), confidence over the coloured count instead of all pixels.
// Each cover's five chromas (18, 19, 38, 76, 152) are superincreasing, so no two bins ever weigh the same
// and the peak is never a tie that rotation would break differently; every coloured bin then holds far
// over 60 % of the mean, so the span never sits on its floor.
void wheelSymmetriesHoldOnRandomCovers() {
    constexpr uint32_t kChromas[5] = {18u, 19u, 38u, 76u, 152u};
    Rng rng(0x6E0DE505u);
    int trials = 0;
    for (int t = 0; t < 400; ++t) {
        std::vector<uint32_t> cover;
        for (uint32_t d : kChromas) cover.push_back(colouredPixel(rng, d));
        const uint32_t greys = rng.below(12u);
        std::vector<uint32_t> withGreys = cover;
        for (uint32_t i = 0; i < greys; ++i) {
            withGreys.insert(withGreys.begin() +
                                 static_cast<std::ptrdiff_t>(rng.below(static_cast<uint32_t>(withGreys.size()) + 1u)),
                             colourlessPixel(rng));
        }

        float base[3], grey[3], opaque[3], rotated[3], mirrored[3];
        std::vector<uint32_t> opaqueCover, rotatedCover, mirroredCover;
        for (uint32_t p : cover) {
            opaqueCover.push_back(p | 0xFF000000u);
            rotatedCover.push_back(rotateChannels(p));
            mirroredCover.push_back(swapGreenBlue(p));
        }
        if (!palette(cover, base) || !palette(withGreys, grey) || !palette(opaqueCover, opaque) ||
            !palette(rotatedCover, rotated) || !palette(mirroredCover, mirrored)) {
            check(false, "a cover of five coloured pixels has a palette");
            continue;
        }
        ++trials;
        const float expectedConfidence = 5.0f / static_cast<float>(5u + greys);
        check(near(base[2], 1.0f), "five coloured pixels: full confidence");
        check(near(grey[2], expectedConfidence), "greys dilute confidence to coloured / all");
        check(grey[0] == base[0] && grey[1] == base[1], "greys never move the hue or the span");
        check(opaque[0] == base[0] && opaque[1] == base[1] && opaque[2] == base[2], "alpha carries no colour");
        check(turnDistance(rotated[0], base[0] + 1.0f / 3.0f) < 1e-4f, "rotating R, G, B turns the hue by 120°");
        check(near(rotated[1], base[1]), "rotating R, G, B keeps the span");
        check(turnDistance(mirrored[0], 1.0f - base[0]) < 1e-4f, "swapping G and B mirrors the hue");
        check(near(mirrored[1], base[1]), "swapping G and B keeps the span");
    }
    check(trials == 400, "every random cover was checked");
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
    brightestKeptRedIs247();
    darkestKeptRedIs31();
    palestKeptPinkIsSaturation0185();
    brightAndSaturatedPixelsOutweighMany();
    spanReachesABinHolding60PercentOfTheMean();
    oppositeHuesSpanTheWholeWheel();
    evenRedAndRoseStayAtTheSeam();
    blueAndGreenLedHuesLandOnTheWheel();
    onePixelIsEnough();
    wheelSymmetriesHoldOnRandomCovers();
    if (failures) {
        std::printf("%d art palette check(s) failed\n", failures);
        return 1;
    }
    std::printf("art palette: all checks passed\n");
    return 0;
}
