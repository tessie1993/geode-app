// Host-side checks for the live wallpaper's window and frame rate (core/viz/frame/WallpaperWindow.hpp, #605).
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: a surface the size of the display (one page) never pans; a panning surface (many pages)
// puts the window on each page by its offset; a step of 0, -1 or NaN changes nothing; an offset that is not
// a number falls back to the pixels; overscroll is clamped to the surface's edges; the nav bar's slack; bad
// sizes; the raw offsets' parse; and the frame rate pick (min of target and cap, never 0 or NaN).
#include <cmath>
#include <cstdio>
#include <limits>

#include "viz/frame/WallpaperWindow.hpp"

namespace wp = geode::viz::frame::wallpaper;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

bool windowIs(const wp::Window& w, float left, float top, float width, float height) {
    return near(w.left, left) && near(w.top, top) && near(w.width, width) && near(w.height, height);
}

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kDisplayW = 1080.0f;
constexpr float kDisplayH = 2400.0f;

wp::Offsets offsets(float x, float xStep, float xPixels, float y = 0.0f, float yStep = 0.0f, float yPixels = 0.0f) {
    wp::Offsets o;
    o.x = x;
    o.y = y;
    o.xStep = xStep;
    o.yStep = yStep;
    o.xPixels = xPixels;
    o.yPixels = yPixels;
    return o;
}

void singlePage() {
    // The surface is the screen: whatever the launcher reports, the window is the whole surface.
    const wp::Window centred = wp::window(kDisplayW, kDisplayH, kDisplayW, kDisplayH, offsets(0.5f, 1.0f, 0.0f));
    check(windowIs(centred, 0, 0, kDisplayW, kDisplayH), "one page: the window is the whole surface");
    const wp::Window right = wp::window(kDisplayW, kDisplayH, kDisplayW, kDisplayH, offsets(1.0f, 0.0f, -300.0f));
    check(windowIs(right, 0, 0, kDisplayW, kDisplayH), "one page: an offset or pixels never pan a surface the size of the display");
    // Within a nav bar's worth (kPanSlack) the surface is still the screen.
    const float slack = kDisplayH * 1.05f;
    const wp::Window navBar = wp::window(kDisplayW, slack, kDisplayW, kDisplayH, offsets(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -120.0f));
    check(windowIs(navBar, 0, 0, kDisplayW, slack), "a surface within the slack of the display does not pan");
}

void manyPages() {
    // Three pages: the launcher's step is 0.5, offsets 0, 0.5, 1; the surface is twice the display wide.
    const float surfaceW = kDisplayW * 2.0f;
    const float range = surfaceW - kDisplayW;
    const float pages[] = {0.0f, 0.5f, 1.0f};
    for (float x : pages) {
        const wp::Window w = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(x, 0.5f, -x * range));
        check(windowIs(w, x * range, 0, kDisplayW, kDisplayH), "three pages: the window sits on each page by its offset");
    }
    // Five pages (step 0.25), mid-swipe between the second and third: the window follows the offset smoothly.
    const wp::Window swipe = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(0.375f, 0.25f, -405.0f));
    check(windowIs(swipe, 0.375f * range, 0, kDisplayW, kDisplayH), "five pages, mid-swipe: the window follows the offset");
    // The offset is used at full precision, not through xPixels' rounding.
    const wp::Window precise = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(1.0f / 3.0f, 1.0f / 3.0f, -360.0f));
    check(near(precise.left, range / 3.0f), "the offset, not the rounded pixels, places the window");
    // A tall panning surface pans down by yOffset just the same; x stays put.
    const float surfaceH = kDisplayH * 1.5f;
    const wp::Window tall = wp::window(kDisplayW, surfaceH, kDisplayW, kDisplayH, offsets(0.5f, 0.0f, 0.0f, 1.0f, 1.0f, -1200.0f));
    check(windowIs(tall, 0, surfaceH - kDisplayH, kDisplayW, kDisplayH), "a tall surface pans by yOffset");
}

void offsetStep() {
    const float surfaceW = kDisplayW * 2.0f;
    const wp::Window stepped = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(0.5f, 0.5f, -540.0f));
    const float steps[] = {0.0f, -1.0f, kNaN, std::numeric_limits<float>::infinity()};
    for (float step : steps) {
        const wp::Window w = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(0.5f, step, -540.0f, 0.0f, step, 0.0f));
        check(windowIs(w, stepped.left, stepped.top, stepped.width, stepped.height),
              "a step of 0, -1, NaN or infinity changes nothing: a launcher that never set it still pans");
        check(std::isfinite(w.left) && std::isfinite(w.width), "a garbled step never makes the window non-finite");
    }
}

void offsetFallback() {
    const float surfaceW = kDisplayW * 2.0f;
    const wp::Window w = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(kNaN, 0.5f, -270.0f));
    check(windowIs(w, 270.0f, 0, kDisplayW, kDisplayH), "an offset that is not a number falls back to the pixels");
    const wp::Window none = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(kNaN, kNaN, kNaN));
    check(windowIs(none, 0, 0, kDisplayW, kDisplayH), "no usable offset or pixels: the first page");
}

void edges() {
    const float surfaceW = kDisplayW * 2.0f;
    const float range = surfaceW - kDisplayW;
    const wp::Window under = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(-0.2f, 0.5f, 216.0f));
    check(windowIs(under, 0, 0, kDisplayW, kDisplayH), "overscroll past the first page is clamped to the left edge");
    const wp::Window over = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(1.3f, 0.5f, -1404.0f));
    check(windowIs(over, range, 0, kDisplayW, kDisplayH), "overscroll past the last page is clamped to the right edge");
    const wp::Window pixelsUnder = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(kNaN, 0.5f, 100.0f));
    check(near(pixelsUnder.left, 0.0f), "a positive xPixels is clamped to the left edge");
    const wp::Window pixelsOver = wp::window(surfaceW, kDisplayH, kDisplayW, kDisplayH, offsets(kNaN, 0.5f, -5000.0f));
    check(near(pixelsOver.left, range), "an xPixels past the surface is clamped to the right edge");
    check(pixelsOver.left + pixelsOver.width <= surfaceW + 1e-3f, "the window never leaves the surface");
}

void badSizes() {
    const wp::Window zero = wp::window(0.0f, 0.0f, kDisplayW, kDisplayH, offsets(0.5f, 0.5f, 0.0f));
    check(windowIs(zero, 0, 0, 0, 0), "an unknown surface gives a zero window (the edge glow's whole surface)");
    const wp::Window nanSurface = wp::window(kNaN, kDisplayH, kDisplayW, kDisplayH, offsets(0.5f, 0.5f, 0.0f));
    check(near(nanSurface.width, 0.0f) && std::isfinite(nanSurface.left), "a NaN surface size is unknown, not NaN");
    const wp::Window noDisplay = wp::window(kDisplayW * 2.0f, kDisplayH, 0.0f, kNaN, offsets(1.0f, 0.5f, -1080.0f));
    check(windowIs(noDisplay, 0, 0, kDisplayW * 2.0f, kDisplayH), "an unknown display never pans");
}

void parse() {
    const float raw[wp::kOffsetFloats] = {0.25f, 0.75f, 0.5f, 1.0f, -270.0f, -10.0f};
    const wp::Offsets o = wp::parseOffsets(raw, wp::kOffsetFloats);
    check(o.x == 0.25f && o.y == 0.75f && o.xStep == 0.5f && o.yStep == 1.0f && o.xPixels == -270.0f && o.yPixels == -10.0f,
          "the raw offsets are read in onOffsetsChanged's order");
    const wp::Offsets few = wp::parseOffsets(raw, 1);
    check(few.x == 0.25f && std::isnan(few.y) && few.xPixels == 0.0f, "missing floats stay unknown");
    const wp::Offsets none = wp::parseOffsets(nullptr, 6);
    check(std::isnan(none.x) && none.xPixels == 0.0f, "no array: nothing known, no pan");
}

void frameRate() {
    check(near(wp::frameRate(60.0f, 30.0f), 30.0f), "a lower cap lowers the rate");
    check(near(wp::frameRate(60.0f, 120.0f), 60.0f), "a higher cap never raises it past the target");
    check(near(wp::frameRate(60.0f, 0.0f), 60.0f), "a cap of 0 is no cap");
    check(near(wp::frameRate(60.0f, kNaN), 60.0f), "a NaN cap is no cap");
    check(near(wp::frameRate(0.0f, 30.0f), 30.0f), "no target: the cap");
    check(near(wp::frameRate(kNaN, kNaN), wp::kDefaultFps), "nothing usable: the default, never 0 or NaN");
}

}  // namespace

int main() {
    singlePage();
    manyPages();
    offsetStep();
    offsetFallback();
    edges();
    badSizes();
    parse();
    frameRate();
    if (failures != 0) {
        std::printf("%d wallpaper window check(s) failed\n", failures);
        return 1;
    }
    std::printf("wallpaper window: all checks passed\n");
    return 0;
}
