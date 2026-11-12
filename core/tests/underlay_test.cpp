// Host-side checks for the background image bake (core/viz/Underlay, #651). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned: the crop rect and the dim are bit-exact with the Kotlin they replace
// (dev.geode.viz.BackgroundImage's centerCropToAspect and applyDim, re-stated below as references);
// the blur keeps a constant image exactly constant and never changes the size, and softens an edge;
// the clamps are the controls' old ranges (amount and dim 0..1, blur 0..25), with NaN reading as 0.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "api/geode_api.h"
#include "viz/Underlay.hpp"

namespace underlay = geode::viz::underlay;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// BackgroundImage.centerCropToAspect's rect, as the Kotlin computed it (Float maths, toInt()).
underlay::Rect kotlinCrop(int srcW, int srcH, int targetW, int targetH) {
    const float targetAspect = static_cast<float>(targetW) / static_cast<float>(targetH);
    const float srcAspect = static_cast<float>(srcW) / static_cast<float>(srcH);
    if (srcAspect > targetAspect) {
        int cropW = static_cast<int>(static_cast<float>(srcH) * targetAspect);
        cropW = cropW < 1 ? 1 : (cropW > srcW ? srcW : cropW);
        return {(srcW - cropW) / 2, 0, cropW, srcH};
    }
    if (srcAspect < targetAspect) {
        int cropH = static_cast<int>(static_cast<float>(srcW) / targetAspect);
        cropH = cropH < 1 ? 1 : (cropH > srcH ? srcH : cropH);
        return {0, (srcH - cropH) / 2, srcW, cropH};
    }
    return {0, 0, srcW, srcH};
}

// BackgroundImage.applyDim for one pixel, as the Kotlin computed it.
uint32_t kotlinDim(uint32_t p, float dim) {
    const float keep = 1.0f - (dim < 0.0f ? 0.0f : (dim > 1.0f ? 1.0f : dim));
    const uint32_t a = (p >> 24) & 0xFF;
    const auto r = static_cast<uint32_t>(static_cast<int>(static_cast<float>((p >> 16) & 0xFF) * keep));
    const auto g = static_cast<uint32_t>(static_cast<int>(static_cast<float>((p >> 8) & 0xFF) * keep));
    const auto b = static_cast<uint32_t>(static_cast<int>(static_cast<float>(p & 0xFF) * keep));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

bool sameRect(const underlay::Rect& a, const underlay::Rect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool rectIs(const underlay::Rect& r, int x, int y, int w, int h) { return sameRect(r, underlay::Rect{x, y, w, h}); }

void testCrop() {
    // Wide source into a 16:9 target: cut the sides, centred.
    check(rectIs(underlay::cropRect(4000, 1000, 1920, 1080), 1111, 0, 1777, 1000), "wide source crops its sides");
    // Tall source into a 16:9 target: cut top and bottom, centred.
    check(rectIs(underlay::cropRect(1080, 1920, 1920, 1080), 0, 656, 1080, 607), "tall source crops top and bottom");
    // Square source into a portrait target: cut the sides.
    check(rectIs(underlay::cropRect(1000, 1000, 1080, 2400), 275, 0, 450, 1000), "square source into portrait");
    // Square into square, and any equal aspect: the whole source.
    check(rectIs(underlay::cropRect(512, 512, 1080, 1080), 0, 0, 512, 512), "square into square is whole");
    check(rectIs(underlay::cropRect(3840, 2160, 1920, 1080), 0, 0, 3840, 2160), "equal aspect is whole");
    // Degenerate sizes give the whole source; an extreme aspect never gives an empty rect.
    check(rectIs(underlay::cropRect(100, 50, 0, 10), 0, 0, 100, 50), "zero target is whole");
    check(underlay::cropRect(10, 10000, 10000, 1).height >= 1, "extreme aspect never empty");
    // Against the Kotlin over a sweep of shapes.
    const int sizes[] = {1, 3, 17, 240, 607, 1080, 1920, 2400, 4000};
    bool same = true;
    for (int sw : sizes)
        for (int sh : sizes)
            for (int tw : sizes)
                for (int th : sizes) same = same && sameRect(underlay::cropRect(sw, sh, tw, th), kotlinCrop(sw, sh, tw, th));
    check(same, "crop rect matches the Kotlin over a sweep");
    // The C ABI hands the same rect out.
    int xywh[4] = {-1, -1, -1, -1};
    geode_underlay_crop_rect(4000, 1000, 1920, 1080, xywh);
    check(xywh[0] == 1111 && xywh[1] == 0 && xywh[2] == 1777 && xywh[3] == 1000, "C ABI crop rect");
}

void testDim() {
    std::vector<uint32_t> pixels;
    uint32_t seed = 12345u;
    for (int i = 0; i < 4096; ++i) {
        seed = seed * 1664525u + 1013904223u;
        pixels.push_back(seed);
    }
    pixels.push_back(0xFFFFFFFFu);
    pixels.push_back(0x00000000u);
    pixels.push_back(0x80FF7F01u);
    const float dims[] = {0.0f, 0.1f, 0.25f, 0.333f, 0.5f, 0.77f, 0.999f, 1.0f, 1.5f, -0.5f};
    bool exact = true;
    for (float d : dims) {
        std::vector<uint32_t> out = pixels;
        underlay::dim(out.data(), static_cast<int>(out.size()), d);
        for (size_t i = 0; i < pixels.size(); ++i) exact = exact && out[i] == kotlinDim(pixels[i], d);
    }
    check(exact, "dim matches the Kotlin per pixel");
    std::vector<uint32_t> black = {0xFF123456u, 0x7FABCDEFu};
    underlay::dim(black.data(), 2, 1.0f);
    check(black[0] == 0xFF000000u && black[1] == 0x7F000000u, "dim 1 is black with alpha kept");
    // finish() with no blur and no dim is a no-op; with only dim it is exactly the dim.
    std::vector<uint32_t> same = pixels;
    underlay::finish(same.data(), 1, static_cast<int>(same.size()), 0, 0.0f);
    check(same == pixels, "finish with nothing to do leaves the pixels");
    std::vector<uint32_t> dimmed = pixels;
    geode_underlay_finish(dimmed.data(), 1, static_cast<int>(dimmed.size()), 0, 0.4f);
    bool dimOnly = true;
    for (size_t i = 0; i < pixels.size(); ++i) dimOnly = dimOnly && dimmed[i] == kotlinDim(pixels[i], 0.4f);
    check(dimOnly, "finish with dim only is the Kotlin dim");
}

void testBlur() {
    const int sizes[][2] = {{1, 1}, {7, 5}, {64, 36}, {101, 257}, {1080, 607}};
    const int radii[] = {1, 2, 5, 13, 25, 40};
    bool constant = true;
    for (const auto& s : sizes) {
        for (int r : radii) {
            const int w = s[0], h = s[1];
            std::vector<uint32_t> img(static_cast<size_t>(w) * static_cast<size_t>(h), 0xC8406080u);
            underlay::blur(img.data(), w, h, r);
            for (uint32_t p : img) constant = constant && p == 0xC8406080u;
            constant = constant && img.size() == static_cast<size_t>(w) * static_cast<size_t>(h);
        }
    }
    check(constant, "blur keeps a constant image exactly constant at every size and radius");

    // Radius 0 is untouched.
    std::vector<uint32_t> noise(64 * 64);
    uint32_t seed = 99u;
    for (auto& p : noise) {
        seed = seed * 1664525u + 1013904223u;
        p = seed;
    }
    std::vector<uint32_t> copy = noise;
    underlay::blur(copy.data(), 64, 64, 0);
    check(copy == noise, "radius 0 leaves the pixels untouched");

    // A hard black/white edge softens into a monotone ramp, with the far ends kept.
    const int w = 200, h = 20;
    std::vector<uint32_t> edge(static_cast<size_t>(w * h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) edge[static_cast<size_t>(y * w + x)] = x < w / 2 ? 0xFF000000u : 0xFFFFFFFFu;
    underlay::blur(edge.data(), w, h, 9);
    bool monotone = true;
    int mids = 0;
    for (int x = 1; x < w; ++x) {
        const uint32_t prev = edge[static_cast<size_t>(10 * w + x - 1)] & 0xFF;
        const uint32_t cur = edge[static_cast<size_t>(10 * w + x)] & 0xFF;
        monotone = monotone && cur >= prev;
        if (cur > 0 && cur < 255) ++mids;
    }
    check(monotone, "blurred edge is a monotone ramp");
    check(mids >= 8, "blurred edge has a soft ramp");
    check((edge[0] & 0xFF) == 0 && (edge[static_cast<size_t>(w - 1)] & 0xFF) == 255, "far ends kept");
    check((edge[0] >> 24) == 0xFF, "opaque stays opaque");
}

void testClamps() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(underlay::clampAmount(-0.2f) == 0.0f && underlay::clampAmount(0.6f) == 0.6f && underlay::clampAmount(3.0f) == 1.0f,
          "amount clamps to 0..1");
    check(underlay::clampDim(-1.0f) == 0.0f && underlay::clampDim(0.25f) == 0.25f && underlay::clampDim(1.01f) == 1.0f,
          "dim clamps to 0..1");
    check(underlay::clampAmount(nan) == 0.0f && underlay::clampDim(nan) == 0.0f, "NaN reads as 0");
    check(underlay::clampBlur(-3) == 0 && underlay::clampBlur(12) == 12 && underlay::clampBlur(26) == 25, "blur clamps to 0..25");
    check(geode_underlay_blur_max() == 25, "blur max is the old BLUR_RANGE end");
    check(geode_underlay_clamp_blur(99) == 25 && geode_underlay_clamp_amount(2.0f) == 1.0f &&
              geode_underlay_clamp_dim(-2.0f) == 0.0f,
          "C ABI clamps");
}

// The saved names are the ones already in users' prefs; anything else falls back to screen.
void testBlends() {
    check(underlay::kBlendCount == 3 && geode_underlay_blend_count() == 3, "three blends");
    check(underlay::blendOf("SCREEN") == 0 && underlay::blendOf("MULTIPLY") == 1 && underlay::blendOf("ADD") == 2,
          "saved names read back as the wire ints");
    for (int i = 0; i < underlay::kBlendCount; ++i) {
        check(underlay::blendOf(underlay::blendName(i)) == i, "a blend's name reads back as itself");
    }
    check(underlay::blendOf(nullptr) == 0 && underlay::blendOf("") == 0 && underlay::blendOf("screen") == 0 &&
              underlay::blendOf("OVERLAY") == 0,
          "an unknown name is screen");
    check(underlay::blendOf(underlay::blendName(-1)) == 0 && underlay::blendOf(underlay::blendName(3)) == 0,
          "an index out of range names screen");
    check(geode_underlay_blend_of("ADD") == 2 && geode_underlay_blend_of(geode_underlay_blend_name(1)) == 1, "C ABI blends");
}

// #999: the renderer keeps the RGBA8 bytes GL takes, converted once when the image is set.
void testRgbaBytes() {
    const uint32_t argb[3] = {0x80112233u, 0xFF000000u, 0x00FFFFFFu};
    uint8_t out[12] = {};
    underlay::toRgbaBytes(argb, 3, out);
    const uint8_t want[12] = {0x11, 0x22, 0x33, 0x80, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    bool same = true;
    for (int i = 0; i < 12; i++) same = same && out[i] == want[i];
    check(same, "ARGB ints become R, G, B, A bytes");
    uint8_t untouched[4] = {7, 7, 7, 7};
    underlay::toRgbaBytes(nullptr, 1, untouched);
    check(untouched[0] == 7 && untouched[3] == 7, "a null source writes nothing");
}

}  // namespace

int main() {
    testRgbaBytes();
    testCrop();
    testDim();
    testBlur();
    testClamps();
    testBlends();
    if (failures) {
        std::printf("underlay_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("underlay_test: ok\n");
    return 0;
}
