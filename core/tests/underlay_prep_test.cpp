// Host-side checks for the background image prep (core/viz/UnderlayPrep, #756), through the C ABI the JNI
// calls and the pure helpers behind it. Built and run by CI ("Native host tests" in
// .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned: the decode sample size (the old Kotlin loop); the centre crop at 16:9, 9:16 and 1:1
// targets from landscape, portrait and square sources; that the blur never leaves the source's range and
// keeps the mean within a few levels; dim 0 is the identity and dim 1 is black with alpha kept; odd sizes,
// a 1-pixel image and bad arguments; and that pixels stay 0xAARRGGBB (the channel never moves).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "viz/UnderlayPrep.hpp"

using namespace geode::viz::underlay;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

uint32_t argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) { return (a << 24) | (r << 16) | (g << 8) | b; }
uint32_t chanR(uint32_t p) { return (p >> 16) & 0xFFu; }
uint32_t chanG(uint32_t p) { return (p >> 8) & 0xFFu; }
uint32_t chanB(uint32_t p) { return p & 0xFFu; }
uint32_t chanA(uint32_t p) { return p >> 24; }

// A deterministic opaque image with structure in every channel.
std::vector<uint32_t> pattern(int w, int h) {
    std::vector<uint32_t> px(static_cast<size_t>(w) * h);
    uint32_t seed = 12345;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const uint32_t noise = (seed >> 24) & 0x3Fu;
            const uint32_t r = static_cast<uint32_t>((x * 255) / std::max(1, w - 1));
            const uint32_t g = static_cast<uint32_t>((y * 255) / std::max(1, h - 1));
            const uint32_t b = ((x / 3 + y / 3) % 2 ? 200u : 40u) + noise / 4;
            px[static_cast<size_t>(y) * w + x] = argb(255, r, g, std::min(255u, b));
        }
    }
    return px;
}

double meanOf(const std::vector<uint32_t>& px, uint32_t (*chan)(uint32_t)) {
    double s = 0.0;
    for (uint32_t p : px) s += chan(p);
    return s / static_cast<double>(px.size());
}

void testSampleSize() {
    check(geode_underlay_sample_size(1000, 1000, 1000, 1000) == 1, "sample: same size decodes whole");
    check(geode_underlay_sample_size(1999, 1999, 1000, 1000) == 1, "sample: just under 2x stays 1");
    check(geode_underlay_sample_size(2000, 2000, 1000, 1000) == 2, "sample: exactly 2x halves");
    check(geode_underlay_sample_size(4000, 3000, 1080, 1920) == 1, "sample: limited by the tighter axis");
    check(geode_underlay_sample_size(8000, 8000, 1000, 1000) == 8, "sample: 8x");
    check(geode_underlay_sample_size(8000, 8000, 999, 1000) == 8, "sample: power of two only");
    check(geode_underlay_sample_size(12000, 9000, 1920, 1080) == 4, "sample: 12000x9000 into 1080p");
    check(geode_underlay_sample_size(100, 100, 1920, 1080) == 1, "sample: smaller than target");
    check(geode_underlay_sample_size(0, 100, 10, 10) == 1, "sample: empty source");
    check(geode_underlay_sample_size(100, 100, 0, 10) == 1, "sample: empty target");
    check(geode_underlay_sample_size(2147483647, 2147483647, 1, 1) == (1 << 30), "sample: no overflow at INT_MAX");
    // The same loop the Kotlin had, over a sweep.
    for (int s = 1; s <= 5000; s += 37) {
        for (int t = 1; t <= 1200; t += 101) {
            int expect = 1;
            while (s / (expect * 2) >= t && (s * 3 / 4) / (expect * 2) >= t) expect *= 2;
            if (geode_underlay_sample_size(s, s * 3 / 4, t, t) != expect) {
                check(false, "sample: matches the old Kotlin loop");
                return;
            }
        }
    }
}

void testCrop() {
    // 16:9 target from a 4:3 landscape: the source is narrower, so the height is cropped.
    Rect r = centreCrop(1600, 1200, 1920, 1080);
    check(r.w == 1600 && r.h == 900 && r.x == 0 && r.y == 150, "crop 16:9 from 4:3: centred height");
    // 16:9 target from a 21:9-ish panorama: crop width.
    r = centreCrop(2560, 1080, 1920, 1080);
    check(r.h == 1080 && r.w == 1920 && r.x == 320 && r.y == 0, "crop 16:9 from wide: centred width");
    // 9:16 target from a landscape photo.
    r = centreCrop(4000, 3000, 1080, 1920);
    check(r.h == 3000 && r.w == 1687 && r.x == (4000 - 1687) / 2 && r.y == 0, "crop 9:16 from 4:3");
    // 9:16 from a taller portrait (9:20).
    r = centreCrop(900, 2000, 1080, 1920);
    check(r.w == 900 && r.h == 1600 && r.x == 0 && r.y == 200, "crop 9:16 from 9:20");
    // 1:1 from both orientations, and the identity.
    r = centreCrop(1920, 1080, 512, 512);
    check(r.w == 1080 && r.h == 1080 && r.x == 420 && r.y == 0, "crop 1:1 from landscape");
    r = centreCrop(1080, 1920, 512, 512);
    check(r.w == 1080 && r.h == 1080 && r.x == 0 && r.y == 420, "crop 1:1 from portrait");
    r = centreCrop(777, 777, 3, 3);
    check(r.w == 777 && r.h == 777 && r.x == 0 && r.y == 0, "crop 1:1 from square is whole");
    // Odd sizes and a sliver: never empty, never outside the image.
    r = centreCrop(1, 1, 1920, 1080);
    check(r.w == 1 && r.h == 1 && r.x == 0 && r.y == 0, "crop 1x1 stays 1x1");
    r = centreCrop(3, 1001, 16, 9);
    check(r.w == 3 && r.h >= 1 && r.y + r.h <= 1001 && r.y >= 0, "crop sliver stays inside");
    for (int w = 1; w < 60; w += 7) {
        for (int h = 1; h < 60; h += 5) {
            for (const auto& t : {std::pair{16, 9}, std::pair{9, 16}, std::pair{1, 1}, std::pair{7, 3}}) {
                const Rect c = centreCrop(w, h, t.first, t.second);
                if (c.w < 1 || c.h < 1 || c.x < 0 || c.y < 0 || c.x + c.w > w || c.y + c.h > h) {
                    check(false, "crop sweep: inside and non-empty");
                    return;
                }
                // Centred: the margins differ by at most one pixel.
                if (std::abs((w - c.x - c.w) - c.x) > 1 || std::abs((h - c.y - c.h) - c.y) > 1) {
                    check(false, "crop sweep: centred");
                    return;
                }
            }
        }
    }
}

void testPrepareCropsCentre() {
    // A 30x10 image whose middle 10x10 is green and sides red: a 1:1 target sees only green.
    std::vector<uint32_t> src(30 * 10, argb(255, 255, 0, 0));
    for (int y = 0; y < 10; ++y) {
        for (int x = 10; x < 20; ++x) src[static_cast<size_t>(y) * 30 + x] = argb(255, 0, 255, 0);
    }
    std::vector<uint32_t> out(10 * 10, 0);
    check(geode_underlay_prepare(src.data(), 30, 10, 10, 10, 0, 0.0f, out.data(), 100) == 100, "prepare: writes count");
    bool allGreen = std::all_of(out.begin(), out.end(), [](uint32_t p) { return p == argb(255, 0, 255, 0); });
    check(allGreen, "prepare: 1:1 crop of 3:1 keeps exactly the centre, untouched");
    // Same at a resize: 5x5 target from the same centre.
    std::vector<uint32_t> small(25, 0);
    geode_underlay_prepare(src.data(), 30, 10, 5, 5, 0, 0.0f, small.data(), 25);
    allGreen = std::all_of(small.begin(), small.end(), [](uint32_t p) { return p == argb(255, 0, 255, 0); });
    check(allGreen, "prepare: crop then shrink stays inside the centre");
}

void testChannelOrder() {
    // One opaque colour per channel survives a resize unswapped: R stays in bits 16..23 and so on.
    const uint32_t c = argb(255, 10, 128, 250);
    std::vector<uint32_t> src(7 * 5, c);
    std::vector<uint32_t> out(16 * 9, 0);
    geode_underlay_prepare(src.data(), 7, 5, 16, 9, 3, 0.0f, out.data(), 16 * 9);
    check(std::all_of(out.begin(), out.end(), [c](uint32_t p) { return p == c; }), "order: flat ARGB colour is exact");
}

void testBlur() {
    const int w = 97, h = 61;  // odd on purpose
    const std::vector<uint32_t> src = pattern(w, h);
    for (int radius : {1, 4, 15, 31, 200}) {
        std::vector<uint32_t> out(static_cast<size_t>(w) * h, 0);
        check(geode_underlay_prepare(src.data(), w, h, w, h, radius, 0.0f, out.data(), w * h) == w * h, "blur: writes");
        bool bounded = true;
        for (auto chan : {chanR, chanG, chanB}) {
            uint32_t lo = 255, hi = 0, olo = 255, ohi = 0;
            for (uint32_t p : src) lo = std::min(lo, chan(p)), hi = std::max(hi, chan(p));
            for (uint32_t p : out) olo = std::min(olo, chan(p)), ohi = std::max(ohi, chan(p));
            bounded = bounded && olo >= lo && ohi <= hi;
            const double drift = std::abs(meanOf(out, chan) - meanOf(src, chan));
            if (drift > 4.0) {
                std::printf("  radius %d mean drift %.2f\n", radius, drift);
                check(false, "blur: keeps the mean within 4 levels");
            }
        }
        check(bounded, "blur: never leaves the source's range");
        check(std::all_of(out.begin(), out.end(), [](uint32_t p) { return chanA(p) == 255; }), "blur: opaque stays opaque");
    }
    // It actually blurs: the checkerboard's neighbour contrast in blue falls.
    std::vector<uint32_t> out(static_cast<size_t>(w) * h, 0);
    geode_underlay_prepare(src.data(), w, h, w, h, 8, 0.0f, out.data(), w * h);
    auto contrast = [&](const std::vector<uint32_t>& px) {
        double s = 0.0;
        for (int y = 0; y < h; ++y) {
            for (int x = 1; x < w; ++x) {
                s += std::abs(static_cast<int>(chanB(px[static_cast<size_t>(y) * w + x])) -
                              static_cast<int>(chanB(px[static_cast<size_t>(y) * w + x - 1])));
            }
        }
        return s;
    };
    check(contrast(out) < contrast(src) * 0.5, "blur: radius 8 halves neighbour contrast at least");
    // Radius 0 (and negative) is no blur.
    geode_underlay_prepare(src.data(), w, h, w, h, 0, 0.0f, out.data(), w * h);
    check(out == src, "blur: radius 0 is the identity at the same size");
    geode_underlay_prepare(src.data(), w, h, w, h, -3, 0.0f, out.data(), w * h);
    check(out == src, "blur: negative radius is no blur");
    check(blurDivisor(0) == 1 && blurDivisor(1) == 2 && blurDivisor(31) == 32 && blurDivisor(1000) == kMaxBlurDivisor,
          "blur: divisor 1 + radius, capped at 32");
}

void testDim() {
    const int w = 13, h = 7;
    std::vector<uint32_t> src = pattern(w, h);
    src[3] = argb(128, 200, 100, 50);  // a translucent pixel keeps its alpha
    std::vector<uint32_t> out(static_cast<size_t>(w) * h, 0);
    geode_underlay_prepare(src.data(), w, h, w, h, 0, 0.0f, out.data(), w * h);
    check(out == src, "dim 0: identity");
    geode_underlay_prepare(src.data(), w, h, w, h, 0, 1.0f, out.data(), w * h);
    bool black = true;
    for (size_t i = 0; i < out.size(); ++i) black = black && out[i] == (src[i] & 0xFF000000u);
    check(black, "dim 1: black, alpha kept");
    geode_underlay_prepare(src.data(), w, h, w, h, 0, 5.0f, out.data(), w * h);
    check(out[0] == (src[0] & 0xFF000000u), "dim > 1 clamps to black");
    // Half: each channel truncates like the old Kotlin (int)(c * 0.5f).
    std::vector<uint32_t> one{argb(255, 255, 101, 3)};
    applyDim(one.data(), 1, 0.5f);
    check(one[0] == argb(255, 127, 50, 1), "dim 0.5: truncates per channel");
    // NaN leaves pixels alone.
    std::vector<uint32_t> nan{argb(255, 9, 9, 9)};
    applyDim(nan.data(), 1, std::nanf(""));
    check(nan[0] == argb(255, 9, 9, 9), "dim NaN: untouched");
}

void testOddAndTiny() {
    // A 1-pixel image fills any target with its colour, through crop, resize, blur and no dim.
    const uint32_t c = argb(255, 33, 66, 99);
    std::vector<uint32_t> out(17 * 31, 0);
    check(geode_underlay_prepare(&c, 1, 1, 17, 31, 5, 0.0f, out.data(), 17 * 31) == 17 * 31, "1px: writes");
    check(std::all_of(out.begin(), out.end(), [c](uint32_t p) { return p == c; }), "1px: flat fill");
    // A 1-pixel target from an odd image: the mean of its crop.
    const std::vector<uint32_t> src = pattern(9, 3);
    uint32_t px = 0;
    check(geode_underlay_prepare(src.data(), 9, 3, 1, 1, 0, 0.0f, &px, 1) == 1, "1px target: writes");
    check(chanA(px) == 255, "1px target: opaque");
    // Odd up and down sizes stay bounded.
    for (const auto& t : {std::pair{3, 5}, std::pair{101, 57}, std::pair{1, 64}, std::pair{64, 1}}) {
        std::vector<uint32_t> o(static_cast<size_t>(t.first) * t.second, 0);
        const int n = geode_underlay_prepare(src.data(), 9, 3, t.first, t.second, 2, 0.25f, o.data(),
                                             static_cast<int>(o.size()));
        check(n == static_cast<int>(o.size()), "odd sizes: writes all");
    }
    // Transparent neighbours don't bleed their hidden colour into an opaque pixel.
    // 2x2: left column opaque blue, right column fully transparent red, averaged to one pixel.
    std::vector<uint32_t> half{argb(255, 0, 0, 255), argb(0, 255, 0, 0), argb(255, 0, 0, 255), argb(0, 255, 0, 0)};
    uint32_t mixed = 0;
    geode_underlay_prepare(half.data(), 2, 2, 1, 1, 0, 0.0f, &mixed, 1);
    check(chanR(mixed) == 0 && chanB(mixed) == 255 && chanA(mixed) == 128, "premultiplied: no hidden-colour bleed");
}

void testBadArgs() {
    const uint32_t c = argb(255, 1, 2, 3);
    uint32_t out[4] = {7, 7, 7, 7};
    check(geode_underlay_prepare(nullptr, 1, 1, 2, 2, 0, 0.0f, out, 4) == 0, "bad: null src");
    check(geode_underlay_prepare(&c, 1, 1, 2, 2, 0, 0.0f, nullptr, 4) == 0, "bad: null out");
    check(geode_underlay_prepare(&c, 0, 1, 2, 2, 0, 0.0f, out, 4) == 0, "bad: empty src");
    check(geode_underlay_prepare(&c, 1, 1, 0, 2, 0, 0.0f, out, 4) == 0, "bad: empty target");
    check(geode_underlay_prepare(&c, 1, 1, 2, 2, 0, 0.0f, out, 3) == 0, "bad: capacity short");
    check(out[0] == 7 && out[3] == 7, "bad: out untouched");
}

}  // namespace

int main() {
    testSampleSize();
    testCrop();
    testPrepareCropsCentre();
    testChannelOrder();
    testBlur();
    testDim();
    testOddAndTiny();
    testBadArgs();
    if (failures) {
        std::printf("%d underlay prep check(s) failed\n", failures);
        return 1;
    }
    std::printf("underlay prep: all checks passed\n");
    return 0;
}
