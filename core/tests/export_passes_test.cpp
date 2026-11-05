// Host-side checks for core/viz/frame/ExportPassMath (#442): the maths of the export post-passes that moved
// from Kotlin GL to native Passes (the watermark, a loop's seam, a Studio transition). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: every value equals what the Kotlin computed before the move (the expected numbers below
// were worked from the Kotlin expressions, float for float), because the acceptance is the same frames
// before and after.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "viz/frame/ExportPassMath.hpp"

namespace wm = geode::viz::frame::exportmath::watermark;
namespace seam = geode::viz::frame::exportmath::seam;
namespace tr = geode::viz::frame::exportmath::transition;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool same(float a, float b) { return a == b; }

// Kotlin's Integer.highestOneBit((v - 1).coerceAtLeast(1)) shl 1, by brute force.
int kotlinNextPowerOfTwo(int value) {
    int v = value - 1 < 1 ? 1 : value - 1;
    int high = 1;
    for (int bit = 30; bit >= 0; bit--) {
        if (v & (1 << bit)) {
            high = 1 << bit;
            break;
        }
    }
    return high << 1;
}

void nextPowerOfTwoMatchesKotlin() {
    for (int v = -4; v <= 9000; v++) {
        if (wm::nextPowerOfTwo(v) != kotlinNextPowerOfTwo(v)) {
            std::printf("  nextPowerOfTwo(%d) = %d, Kotlin %d\n", v, wm::nextPowerOfTwo(v), kotlinNextPowerOfTwo(v));
            check(false, "nextPowerOfTwo matches Kotlin's highestOneBit form");
            return;
        }
    }
    check(wm::nextPowerOfTwo(64) == 64, "a power of two stays itself");
    check(wm::nextPowerOfTwo(65) == 128, "one past rounds up");
    check(wm::nextPowerOfTwo(1) == 2, "under two is two, as Kotlin's was");
}

void textPxFollowsTheShortSide() {
    check(same(wm::textPx(1080), 1080 * 0.045f), "1080p short side: 4.5 % of it");
    check(same(wm::textPx(2160), 2160 * 0.045f), "4K short side: 4.5 % of it");
    check(same(wm::textPx(100), 14.0f), "a tiny frame keeps the 14 px floor");
    check(same(wm::textPx(0), 14.0f), "no frame keeps the floor");
}

void tileIsAPowerOfTwoAboveTheText() {
    // 300 px of text: 300 * 1.35 = 405 -> 512 wide, 256 high.
    check(wm::tileWidth(300.0f) == 512, "the text plus its gap rounds up to a power of two");
    check(wm::tileHeight(512) == 256, "the height is half the width, a power of two");
    // Short text keeps the 64 px floor.
    check(wm::tileWidth(10.0f) == 64, "short text keeps the 64 px tile floor");
    check(wm::tileHeight(64) == 32, "and half of it high");
    // Truncation toward zero, as Kotlin's toInt: 47 * 1.35 = 63.45 -> 63, then the floor.
    check(wm::tileWidth(47.0f) == 64, "47 * 1.35 truncates to 63, floored to 64");
    check(wm::tileWidth(std::numeric_limits<float>::quiet_NaN()) == 64, "NaN width truncates to 0, floored");
    for (float w = 0.0f; w < 4000.0f; w += 7.25f) {
        const int t = wm::tileWidth(w);
        check(t >= 64 && (t & (t - 1)) == 0, "every tile width is a power of two of at least 64");
        check(static_cast<float>(t) >= static_cast<float>(static_cast<int>(w * 1.35f)), "and holds the text plus its gap");
    }
}

void tilesCountFramePixelsOverTilePixels() {
    const auto t = wm::tiles(1920, 1080, 512, 256);
    check(same(t.u, 1920.0f / 512.0f) && same(t.v, 1080.0f / 256.0f), "landscape tiles are frame / tile per axis");
    const auto p = wm::tiles(720, 1280, 256, 128);
    check(same(p.u, 720.0f / 256.0f) && same(p.v, 10.0f), "portrait tiles likewise");
    const auto none = wm::tiles(1920, 1080, 0, 0);
    check(none.u == 0.0f && none.v == 0.0f, "no tile, no tiling");
}

void seamWeightReachesBothEnds() {
    check(same(seam::weight(0, 30), 0.0f), "no weight on the first closing frame");
    check(same(seam::weight(29, 30), 1.0f), "all of it on the last");
    check(same(seam::weight(10, 30), 10.0f / 29.0f), "linear between, as tail / (frames - 1f)");
    check(same(seam::weight(0, 1), 1.0f), "a one-frame seam is the stashed frame");
    check(same(seam::weight(5, 0), 1.0f), "no seam weighs one, as the Kotlin did");
    check(same(seam::weight(40, 30), 1.0f), "past the end clamps to one, as the uniform always was");
    check(same(seam::weight(-3, 30), 0.0f), "before the start clamps to zero");
    float last = -1.0f;
    for (int tail = 0; tail < 60; tail++) {
        const float w = seam::weight(tail, 60);
        check(w >= last, "the dissolve only rises");
        last = w;
    }
}

void transitionProgressFromTheFirstFrame() {
    check(same(tr::progress(1'000'000, 1'000'000, 500'000), 0.0f), "the clip's first frame is the start");
    check(same(tr::progress(1'250'000, 1'000'000, 500'000), 0.5f), "half way through the duration is 0.5");
    check(same(tr::progress(1'500'000, 1'000'000, 500'000), 1.0f), "the end is 1");
    check(same(tr::progress(9'000'000, 1'000'000, 500'000), 1.0f), "past the end stays 1");
    check(same(tr::progress(900'000, 1'000'000, 500'000), 0.0f), "an earlier time clamps to 0");
    check(same(tr::progress(5, 0, 0), 1.0f), "no duration is already over");
    check(same(tr::progress(33'333, 0, 1'000'000), static_cast<float>(33'333) / static_cast<float>(1'000'000)),
          "long to float, then float division, as Kotlin");
}

void transitionRatioAndPassthrough() {
    check(same(tr::ratio(1920, 1080), 1920.0f / 1080.0f), "ratio is width over height");
    check(same(tr::ratio(640, 0), 640.0f), "a zero height reads as one pixel");
    check(tr::passthrough(false, 0.3f), "no outgoing frame draws the clip plain");
    check(tr::passthrough(true, 1.0f), "the end draws the clip plain");
    check(!tr::passthrough(true, 0.99f), "inside the window it blends");
}

// ---- Added by the mutation pass on #552. Every expected value is a hand-worked literal. ----

// Kills: truncate -> round or ceil (95.2 * 1.35 = 128.52 would round up past 128); kTileSpacing nudged
// down (96 * 1.35 = 129.6 must spill into the 256 tile; at 1.3 it would stay 128).
void tileWidthTruncatesTextPlusGapBeforeRounding() {
    check(wm::tileWidth(95.2f) == 128, "128.52 px of text plus gap truncates to 128, already a power of two");
    check(wm::tileWidth(96.0f) == 256, "129.6 px of text plus gap needs the 256 tile");
    check(wm::tileWidth(94.0f) == 128, "126.9 px of text plus gap fits the 128 tile");
    check(wm::tileWidth(-500.0f) == 64, "a negative width keeps the 64 px floor");
}

// Kills: the 1 << 14 cap's signed overflow. nextPowerOfTwo(v > 2^30) shifts 2^30 left once, which C++20
// defines as INT_MIN (cppreference, "Bitwise shift operators", since C++20), and min(INT_MIN, cap) keeps
// it. The header promises a power of two of at least kMinTilePx for any measured width.
// EXPECTED TO FAIL on #552 as written for widths above about 8e8 px: a latent defect, not reachable
// through Paint.measureText at a real frame's text size today.
void tileWidthStaysAPositivePowerOfTwoForAbsurdWidths() {
    const float absurd[] = {12'000.0f, 1.0e6f, 8.0e8f, 1.0e9f, 1.0e12f, std::numeric_limits<float>::infinity()};
    for (float w : absurd) {
        const int t = wm::tileWidth(w);
        if (!(t >= 64 && (t & (t - 1)) == 0)) {
            std::printf("  tileWidth(%g) = %d\n", static_cast<double>(w), t);
            check(false, "the tile width stays a power of two of at least 64 for any text width");
        }
    }
}

// Kills: tileWidth / 2 -> / 4 or no halving (every caller passes tileWidth()'s power-of-two output).
void tileHeightIsHalfTheTileWidth() {
    const int widths[] = {64, 128, 256, 512, 1024, 2048, 4096};
    const int heights[] = {32, 64, 128, 256, 512, 1024, 2048};
    for (int i = 0; i < 7; i++) check(wm::tileHeight(widths[i]) == heights[i], "a power-of-two tile is half as high as wide");
}

// Kills: kTextFraction or kMinTextPx nudged, max -> min; pins 4K and odd short sides and the floor's edge.
void textPxAtFourKAndOddFrames() {
    check(std::fabs(wm::textPx(2160) - 97.2f) < 1e-4f, "4K's short side gives 97.2 px of wordmark");
    check(std::fabs(wm::textPx(1081) - 48.645f) < 1e-4f, "an odd 1081 px short side gives 48.645 px");
    // 14 / 0.045 = 311.1: a 311 px short side is under the floor, 312 just over it.
    check(wm::textPx(311) == 14.0f, "a 311 px short side is still on the 14 px floor");
    check(wm::textPx(312) > 14.0f, "a 312 px short side is just above it");
}

// Kills: `||` -> `&&` in the zero-tile guard (one zero side would divide by zero), u and v swapped,
// integer division. Odd and 4K frames in both orientations; each quotient is exact in binary.
void tilesAtFourKAndOddFramesBothOrientations() {
    const auto land4k = wm::tiles(3840, 2160, 512, 256);
    check(land4k.u == 7.5f && land4k.v == 8.4375f, "4K landscape: 7.5 tiles across, 8.4375 down");
    const auto port4k = wm::tiles(2160, 3840, 512, 256);
    check(port4k.u == 4.21875f && port4k.v == 15.0f, "4K portrait: 4.21875 across, 15 down");
    const auto odd = wm::tiles(1919, 1081, 512, 256);
    check(odd.u == 3.748046875f && odd.v == 4.22265625f, "odd 1919 x 1081: fractional tiles, no rounding");
    const auto oddPortrait = wm::tiles(1081, 1919, 256, 128);
    check(oddPortrait.u == 4.22265625f && oddPortrait.v == 14.9921875f, "odd portrait likewise");
    const auto noWidth = wm::tiles(1920, 1080, 0, 256);
    check(noWidth.u == 0.0f && noWidth.v == 0.0f, "a tile with no width tiles nothing");
    const auto noHeight = wm::tiles(1920, 1080, 512, 0);
    check(noHeight.u == 0.0f && noHeight.v == 0.0f, "a tile with no height tiles nothing");
    const auto negative = wm::tiles(1920, 1080, -512, 256);
    check(negative.u == 0.0f && negative.v == 0.0f, "a negative tile tiles nothing");
}

// Kills: `seamFrames > 1` -> `> 2` (a two-frame seam must still go 0 then 1), `- 1.0f` -> `+ 1.0f` or
// dropped. The dissolve's two weights (stashed opening frame w, closing frame 1 - w) mirror about the
// seam's middle, so closing frame `tail` and frame `n - 1 - tail` together weigh exactly one.
void seamWeightsMirrorAndStepEvenly() {
    check(seam::weight(0, 2) == 0.0f && seam::weight(1, 2) == 1.0f, "a two-frame seam is one plain frame each side");
    check(seam::weight(1, 3) == 0.5f, "a three-frame seam's middle frame is half and half");
    check(seam::weight(1, 5) == 0.25f && seam::weight(3, 5) == 0.75f, "a five-frame seam steps by quarters");
    for (int n = 2; n <= 240; n++) {
        check(seam::weight(n - 1, n) == 1.0f, "every seam ends exactly on the stashed frame");
        check(seam::weight(0, n) == 0.0f, "and starts exactly on the render");
        for (int tail = 0; tail < n; tail++) {
            const float w = seam::weight(tail, n);
            const float mirror = seam::weight(n - 1 - tail, n);
            if (std::fabs(w + mirror - 1.0f) > 1e-6f) {
                std::printf("  weight(%d,%d) + weight(%d,%d) = %g\n", tail, n, n - 1 - tail, n, static_cast<double>(w + mirror));
                check(false, "the dissolve mirrors about the seam's middle");
                return;
            }
        }
    }
    check(seam::weight(3, -4) == 1.0f, "a negative seam length weighs one, like no seam");
}

// Kills: `durationUs <= 0` -> `< 0` (zero duration at the origin is 0 / 0 = NaN, which std::clamp passes
// through) or `== 0` (a negative duration would run backwards and clamp to 0).
void transitionWithNoDurationIsOverAtEveryFrame() {
    check(tr::progress(1'000'000, 1'000'000, 0) == 1.0f, "zero duration on the clip's first frame is already over");
    check(tr::progress(1'000'005, 1'000'000, -1) == 1.0f, "a negative duration is already over");
    check(tr::progress(999'000, 1'000'000, -500'000) == 1.0f, "even before the origin");
}

// Kills: float(pts) - float(origin) in place of float(pts - origin). An hour into a render the microsecond
// clock is 3.6e12, where a float's step is 262144 us, so converting before subtracting loses a whole
// half-second transition; the difference must be taken in 64 bits first, as the Kotlin did.
void transitionProgressHoldsAnHourIntoTheRender() {
    const int64_t hour = 3'600'000'000'000LL;
    check(tr::progress(hour, hour, 500'000) == 0.0f, "an hour in, the clip's first frame is still the start");
    check(tr::progress(hour + 250'000, hour, 500'000) == 0.5f, "and half its duration later is 0.5");
    check(tr::progress(hour + 125'000, hour, 500'000) == 0.25f, "and a quarter of it is 0.25");
}

// Kills: either clamp bound dropped or swapped; passthrough's end test moved to the start (progress >= 0).
void transitionProgressRisesFromZeroToOne() {
    float last = -1.0f;
    for (int64_t t = -100'000; t <= 700'000; t += 12'500) {
        const float p = tr::progress(t, 0, 500'000);
        check(p >= 0.0f && p <= 1.0f, "progress stays inside 0..1");
        check(p >= last, "and never falls back");
        last = p;
    }
    check(!tr::passthrough(true, 0.0f), "the first frame of a transition with an outgoing frame blends");
}

}  // namespace

int main() {
    nextPowerOfTwoMatchesKotlin();
    textPxFollowsTheShortSide();
    tileIsAPowerOfTwoAboveTheText();
    tilesCountFramePixelsOverTilePixels();
    seamWeightReachesBothEnds();
    transitionProgressFromTheFirstFrame();
    transitionRatioAndPassthrough();
    tileWidthTruncatesTextPlusGapBeforeRounding();
    tileWidthStaysAPositivePowerOfTwoForAbsurdWidths();
    tileHeightIsHalfTheTileWidth();
    textPxAtFourKAndOddFrames();
    tilesAtFourKAndOddFramesBothOrientations();
    seamWeightsMirrorAndStepEvenly();
    transitionWithNoDurationIsOverAtEveryFrame();
    transitionProgressHoldsAnHourIntoTheRender();
    transitionProgressRisesFromZeroToOne();
    if (failures == 0) std::printf("export_passes_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
