// Host-side checks for core/viz/frame/ExportPassMath (#442): the maths of the export post-passes that moved
// from Kotlin GL to native Passes (the watermark, a loop's seam, a Studio transition). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: every value equals what the Kotlin computed before the move (the expected numbers below
// were worked from the Kotlin expressions, float for float), because the acceptance is the same frames
// before and after.
#include <cmath>
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

}  // namespace

int main() {
    nextPowerOfTwoMatchesKotlin();
    textPxFollowsTheShortSide();
    tileIsAPowerOfTwoAboveTheText();
    tilesCountFramePixelsOverTilePixels();
    seamWeightReachesBothEnds();
    transitionProgressFromTheFirstFrame();
    transitionRatioAndPassthrough();
    if (failures == 0) std::printf("export_passes_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
