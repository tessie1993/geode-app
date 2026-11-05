#pragma once

#include <cstdint>

// The background image's pixel work (#756, ADR 0003): what turns a decoded photo into the exact
// targetW x targetH picture geode_viz_set_underlay_rgba takes. Kotlin (dev.geode.viz.BackgroundImage)
// keeps only the BitmapFactory decode and the Bitmap.getPixels copy; every decision about sizes, the
// crop, the blur and the dim is here, so the live underlay and an export's underlay are one code path.
//
// Pixel format, in and out: Android's Bitmap.getPixels layout, one uint32 per pixel, 0xAARRGGBB
// (non-premultiplied sRGB, alpha in the top byte), row-major, no row padding. That is exactly what
// geode_viz_set_underlay_rgba documents and CompositePass::uploadUnderlay unpacks, so nothing is
// reordered on the way through. The resampling inside works on premultiplied floats, as Android's own
// bitmap scaler does, so a transparent pixel's hidden colour never bleeds into its neighbours.
//
// Pure, allocation only in prepare(), any thread.
namespace geode::viz::underlay {

// The largest blur shrink: a radius past 31 blurs no further (the old Kotlin cap).
inline constexpr int kMaxBlurDivisor = 32;

// The BitmapFactory.Options.inSampleSize to decode a srcW x srcH image with: the largest power of two
// that still leaves the decode at least targetW x targetH in both dimensions, so the crop has real
// pixels without ever holding a full-resolution decode. 1 for any non-positive size.
int sampleSize(int srcW, int srcH, int targetW, int targetH);

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// The centred crop of a w x h image to targetW:targetH's aspect; never larger than the image, never
// empty. The whole image when the aspects already match.
Rect centreCrop(int w, int h, int targetW, int targetH);

// The blur's shrink factor for a radius: 1 + radius, in 1..kMaxBlurDivisor (1 = no blur).
int blurDivisor(int blurRadius);

// Resizes the srcW x srcH window of `src` (row stride `srcStride` pixels) into dst (dstW x dstH, packed):
// an area average along an axis that shrinks, bilinear along one that grows, edges clamped. A same-size
// resize is an exact copy.
void resize(const uint32_t* src, int srcStride, int srcW, int srcH, uint32_t* dst, int dstW, int dstH);

// Darkens each pixel toward black by dim (0 = untouched, 1 = black, clamped), alpha unchanged; each
// channel truncates, as the old Kotlin did. NaN or <= 0 leaves the pixels alone.
void applyDim(uint32_t* pixels, int64_t count, float dim);

// The whole preparation: centre-crop src (w x h) to the target's aspect, resize to targetW x targetH,
// blur by shrinking by blurDivisor(blurRadius) then scaling back up, dim. Writes targetW * targetH
// pixels to out and returns that count, or 0 (writing nothing) for a bad size, a null pointer or
// outCapacity under targetW * targetH.
int prepare(const uint32_t* src, int w, int h, int targetW, int targetH, int blurRadius, float dim, uint32_t* out,
            int64_t outCapacity);

}  // namespace geode::viz::underlay
