#pragma once

#include <cstddef>
#include <cstdint>

namespace geode::viz::underlay {

// The background image under the scene (Visuals › Background): how a decoded picture is cut and
// softened into the underlay `geode_viz_set_underlay_rgba` blends. Pure pixel maths on Android's
// ARGB_8888 ints (0xAARRGGBB, row-major), so live and export bake one look, tested on the host
// (core/tests/underlay_test.cpp). The shell only decodes the bitmap, crops and scales it by the rect
// below with the platform scaler, reads its pixels and stores the choice; blend and amount are not
// baked in here, so changing either never re-decodes.
//
// Ported from Kotlin (dev.geode.viz.BackgroundImage, #651): the crop rect and the dim are bit-exact
// with it; the blur keeps its downscale-then-upscale shape with a box (area) downscale instead of the
// platform's bilinear one (see blur()).

// The blur control's range: a whole number of steps; the image is shrunk by (1 + radius) and brought
// back up.
inline constexpr int kBlurMin = 0;
inline constexpr int kBlurMax = 25;

// Amount (how much of the blend replaces the plain scene) and dim (toward black) are 0..1; a NaN
// reads as 0, so a corrupt pref never reaches the bake or the shader as NaN.
float clampAmount(float amount);
float clampDim(float dim);
int clampBlur(int radius);

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// The centred crop of a srcW x srcH image to targetW:targetH's aspect; never larger than the source,
// never empty for a real source. Equal aspects (and any non-positive size) give the whole source.
Rect cropRect(int srcW, int srcH, int targetW, int targetH);

// Softens `pixels` (width x height) in place: shrink by (1 + clampBlur(radius)), each small pixel the
// rounded mean of its whole cell of the full image, then a bilinear scale back up (pixel centres,
// edges clamped), every channel alpha included.
//
// The old Kotlin path shrank with the platform's bilinear scale, which reads only about four source
// pixels per small pixel however large the cell; the box mean averages every one. So a flat or slowly
// varying picture comes out the same within a level or so, while a detailed one (fine texture, text)
// comes out a little smoother and no longer aliases or shimmers as the radius changes. A constant image
// stays exactly constant and the size never changes. Radius 0 leaves the pixels untouched.
void blur(uint32_t* pixels, int width, int height, int radius);

// Darkens toward black by clampDim(dim) (0 = untouched, 1 = black), alpha unchanged: each colour
// channel c becomes int(float(c) * (1 - dim)), truncated, exactly as BackgroundImage.applyDim did.
void dim(uint32_t* pixels, int count, float dim);

// The whole bake after the crop and scale, in the old order: blur, then dim. Each is skipped at 0.
void finish(uint32_t* pixels, int width, int height, int blurRadius, float dimAmount);

// Android's ARGB_8888 ints (0xAARRGGBB) as GL's RGBA8 bytes, `count` pixels into out[0, 4 * count).
// Done once, when the image is set, so the renderer keeps only the bytes it uploads (#999).
void toRgbaBytes(const uint32_t* argb, size_t count, uint8_t* out);

}  // namespace geode::viz::underlay
