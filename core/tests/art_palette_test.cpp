// The artwork palette (viz/ArtPalette), pinned to the Kotlin `dev.geode.analysis.ArtPalette` it replaces: the
// expected values below are that code's results worked by hand for each cover, so the port reads a cover as the
// app did. Its two readers: Customize › Look's "Take the colours from the artwork" (base hue and span on the
// palette slot, the confidence in its note) and the widget's glow (base hue and span).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/ArtPalette.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

constexpr int32_t px(uint32_t argb) { return static_cast<int32_t>(argb); }

// Each pixel at full saturation and value 0.8 (0xCC), so each weighs 0.8.
constexpr int32_t kRed = px(0xFFCC0000u);        // hue 0, bin 0
constexpr int32_t kRedOrange = px(0xFFCC3100u);  // hue 0.040, bin 1
constexpr int32_t kRose = px(0xFFCC0014u);       // hue 0.984, bin 35
constexpr int32_t kGreen = px(0xFF00CC00u);      // hue 1/3, bin 12
constexpr int32_t kCyan = px(0xFF00CCCCu);       // hue 1/2, bin 18
constexpr int32_t kBlue = px(0xFF0000CCu);       // hue 2/3, bin 24
constexpr int32_t kGrey = px(0xFF808080u);

std::optional<art::Extracted> of(const std::vector<int32_t>& pixels) {
    return art::extract(pixels.data(), static_cast<int>(pixels.size()));
}

void hsvMatchesTheKotlin() {
    float c[3];
    art::hsv(kRed, c);
    check(c[0] == 0.0f && c[1] == 1.0f && near(c[2], 0.8f), "red: hue 0, saturation 1, value 0.8");
    art::hsv(kGrey, c);
    check(c[0] == 0.0f && c[1] == 0.0f && near(c[2], 128.0f / 255.0f), "a grey has hue 0 and no saturation");
    art::hsv(kRose, c);
    check(near(c[0], 1.0f - (20.0f / 255.0f) / 0.8f / 6.0f), "a hue below 0 wraps into the turn");
    art::hsv(px(0xFF000000u), c);
    check(c[0] == 0.0f && c[1] == 0.0f && c[2] == 0.0f, "black is all zero");
    art::hsv(px(0x00CC0000u), c);
    check(c[0] == 0.0f && c[1] == 1.0f && near(c[2], 0.8f), "alpha is ignored");
}

void noColourIsNothing() {
    check(!art::extract(nullptr, 4), "no pixels");
    check(!of({}), "an empty cover");
    check(!of({kGrey, px(0xFF000000u), px(0xFFFFFFFFu)}), "greys, black and white");
    check(!of({px(0xFFFF0000u)}), "a full-value red is blown out (value 1 > 0.97)");
    check(!of({px(0xFFF80000u)}), "value 248/255 is over 0.97");
    check(!of({px(0xFF1E0000u)}), "value 30/255 is under 0.12");
    check(!of({px(0xFFCCABABu)}), "saturation 0.16 is under 0.18");
    check(static_cast<bool>(of({px(0xFFF70000u)})), "value 247/255 counts");
    check(static_cast<bool>(of({px(0xFF200000u)})), "value 32/255 counts");
    check(static_cast<bool>(of({px(0xFFCCA0A0u)})), "saturation 0.22 counts");
}

void oneHueIsItsBinCentre() {
    const auto red = of({kRed, kRed});
    check(red && near(red->baseHue, 0.5f / 36.0f), "red sits at bin 0's centre");
    check(red && near(red->span, 0.06f), "one hot bin spans the minimum 0.06");
    check(red && red->confidence == 1.0f, "every pixel coloured");
    const auto green = of({kGreen});
    check(green && near(green->baseHue, 12.5f / 36.0f), "green at bin 12's centre");
    const auto blue = of({kBlue});
    check(blue && near(blue->baseHue, 24.5f / 36.0f), "blue at bin 24's centre");
    const auto faded = of({px(0x00CC0000u)});
    check(faded && near(faded->baseHue, red ? red->baseHue : -1.0f), "a transparent pixel reads as its colour");
}

void confidenceIsTheColouredShare() {
    const auto quarter = of({kRed, kGrey, kGrey, kGrey});
    check(quarter && quarter->confidence == 0.25f, "one coloured pixel in four");
    check(quarter && near(quarter->baseHue, 0.5f / 36.0f), "the greys do not move the hue");
}

void theHueLeansToTheHeavierNeighbour() {
    // Bin 0 weighs 2.4, bin 1 0.8: shift (0.8 - 0) / 3.2 * 0.5 = 0.125 of a bin.
    const auto lean = of({kRed, kRed, kRed, kRedOrange});
    check(lean && near(lean->baseHue, 0.625f / 36.0f), "leans an eighth of a bin towards bin 1");
    check(lean && near(lean->span, 3.0f / 36.0f), "bin 1 is hot: reach 1, span 3 bins");
    check(lean && lean->confidence == 1.0f, "all coloured");
    // Bin 35's right neighbour is bin 0, across the seam.
    const auto seam = of({kRose, kRose, kRose, kRed});
    check(seam && near(seam->baseHue, 35.625f / 36.0f), "bin 35 leans towards bin 0 across the seam");
    check(seam && near(seam->span, 3.0f / 36.0f), "the reach crosses the seam too");
}

void theSpanIsTheReachOfTheHotBins() {
    // Equal weights: the first bin is the peak, as Kotlin's maxByOrNull picks it.
    const auto tie = of({kGreen, kRed});
    check(tie && near(tie->baseHue, 0.5f / 36.0f), "a tie keeps the first bin");
    check(tie && near(tie->span, 25.0f / 36.0f), "green 12 bins away: span 25 bins");
    const auto opposite = of({kRed, kCyan});
    check(opposite && opposite->span == 1.0f, "half a turn away reaches 37 bins, held to a whole turn");
    // A weak bin under 0.6 of the mean weight is not hot: bin 0 at 30 pixels, bin 12 at one, total 24.8,
    // floor 24.8 / 36 * 0.6 = 0.413 < 0.8, so bin 12 is still hot; at 60 it is 0.813 and is not.
    std::vector<int32_t> many(30, kRed);
    many.push_back(kGreen);
    const auto weakHot = of(many);
    check(weakHot && near(weakHot->span, 25.0f / 36.0f), "one green pixel in 31 is still hot");
    std::vector<int32_t> more(60, kRed);
    more.push_back(kGreen);
    const auto weakCold = of(more);
    check(weakCold && near(weakCold->span, 0.06f), "one green pixel in 61 is under the floor");
}

void theAbiWritesOrLeavesOut() {
    float out[3] = {-1.0f, -1.0f, -1.0f};
    const int32_t grey[] = {kGrey, kGrey};
    check(geode_art_palette_extract(grey, 2, out) == 0, "a grey cover gives nothing");
    check(out[0] == -1.0f && out[1] == -1.0f && out[2] == -1.0f, "and leaves out untouched");
    check(geode_art_palette_extract(nullptr, 3, out) == 0, "no pixels gives nothing");
    const int32_t lean[] = {kRed, kRed, kRed, kRedOrange};
    check(geode_art_palette_extract(lean, 0, out) == 0, "n = 0 gives nothing");
    check(geode_art_palette_extract(lean, 4, nullptr) == 0, "nowhere to write gives nothing");
    check(geode_art_palette_extract(lean, 4, out) == 1, "a coloured cover gives a palette");
    check(near(out[0], 0.625f / 36.0f) && near(out[1], 3.0f / 36.0f) && out[2] == 1.0f,
          "out holds base hue, span, confidence");
}

}  // namespace

int main() {
    hsvMatchesTheKotlin();
    noColourIsNothing();
    oneHueIsItsBinCentre();
    confidenceIsTheColouredShare();
    theHueLeansToTheHeavierNeighbour();
    theSpanIsTheReachOfTheHotBins();
    theAbiWritesOrLeavesOut();
    if (failures == 0) std::printf("art_palette_test: all checks passed\n");
    return failures;
}
