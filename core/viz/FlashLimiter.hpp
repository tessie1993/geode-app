#pragma once

#include <array>

// The output flash limiter's control logic, with no GL: the reference the GPU pass
// (viz/FlashLimitPass, shaders/lib_flash_limit.glsl + flash_control_frag.glsl) mirrors operation for
// operation, and what core/tests/flash_limit_test.cpp proves "no more than three flashes in any one
// second" against. The rule and its constants are in VisualSafety.hpp (safety::kFlash*, kRed*).
//
// Inputs, per tile of the 16 x 9 grid (9 x 16 in portrait), measured on the drawn frame: the mean
// relative luminance, the mean saturated-red value, the mean luminance the tile would have with its
// saturated red guarded away, and the tile's pixel area. Output, per tile: one transform for every
// pixel of the tile, out = mix(in * gain, white, lift) in linear light after an optional red guard.
// The transform is constant over a tile, so the luminance the tile shows is known exactly from its
// measured means, and so is its red: an untouched tile is a byte copy, and a touched one is guarded
// (every pixel near or over the saturation gate pulled to just under it, luminance kept), so its red
// is 0. The region signals the detectors watch are therefore the frame's real ones, not estimates.
//
// Regions: a 3 x 3 grid of windows, each half the frame wide and (rounded up) half high, starting at
// 0, a quarter and a half of the frame each way (on 16 x 9 tiles: 8 x 5 tiles at x 0/4/8, y 0/2/4,
// 28% of the frame each). A flash has to cover enough of the view to matter (WCAG: a quarter of a
// 10-degree field, a few percent of a phone screen at reading distance); averaged over a region, a
// full-contrast change over about 3% of the screen already moves the region by 0.1, and because the
// windows overlap by half, any patch up to a quarter of the screen lies wholly inside one of them,
// so no flash is diluted by straddling a boundary. Small sparkles and moving shapes average out and
// are never touched.
//
// All state goes through the 16-bit fixed point the GPU stores it in (two RGBA8 channels per value),
// so the host test runs on exactly the values the device holds.
namespace geode::viz::flash_limit {

constexpr float kQuantSteps = 65535.0f;
constexpr int kTiles = 144;  // 16 x 9 either way round
constexpr int kRegions = 9;
constexpr int kSlots = 5;    // safety::kFlashEventsPerWindow
// Ages run 0..kEmptyAge seconds; a free slot holds kEmptyAge.
constexpr float kEmptyAge = 2.0f;
// Each pass of the hold/guard fixed point locks at least one more (region, kind) or ends.
constexpr int kMaxIterations = 2 * kRegions + 2;

// Round to the nearest 16-bit step (flPack16), or down (ages: an event never looks older than it is).
float quantise(float v);
float quantiseAge(float age);

// sRGB transfer functions (IEC 61966-2-1) and Rec.709 relative luminance of linear RGB.
float srgbToLinear(float c);
float linearToSrgb(float c);
float luminance(float r, float g, float b);

// One linear pixel's measures, as the GPU's cell pass takes them.
struct PixelMeasure {
    float lum = 0.0f;
    float red = 0.0f;         // (R - G - B) when saturated red, else 0
    float guardedLum = 0.0f;  // luminance after the red guard
};
PixelMeasure measure(float r, float g, float b);
// The red guard on one linear pixel (in place): red value to 0, luminance kept.
void guard(float& r, float& g, float& b);

struct Layout {
    int cols = 16;
    int rows = 9;
    static Layout forSize(int width, int height);
    // Region r's tile window: [x0, x0 + w) x [y0, y0 + h).
    void region(int r, int& x0, int& y0, int& w, int& h) const;
    bool inRegion(int tile, int r) const;
};

struct TileInput {
    float lum = 0.0f;
    float red = 0.0f;
    float guardedLum = 0.0f;
    float area = 1.0f;
};

// One region's detector for one kind (luminance or red).
struct Track {
    bool up = false;       // the leg in progress rises (its extreme is a maximum)
    bool pending = false;  // luminance only: a falling leg confirmed above the dark limit, not yet counted
    float extreme = 0.0f;
    std::array<float, kSlots> ages{};
};

struct TileState {
    float shown = 0.0f;
    bool releasing = false;
};

struct State {
    bool primed = false;
    std::array<TileState, kTiles> tiles{};
    std::array<Track, kRegions> lum{};
    std::array<Track, kRegions> red{};
};

struct TileTransform {
    float gain = 1.0f;
    float lift = 0.0f;
    bool guard = false;
    bool identity() const { return gain == 1.0f && lift == 0.0f && !guard; }
};

struct FrameResult {
    std::array<TileTransform, kTiles> transforms{};
    // What each region shows this frame (area-weighted), the signals the detectors counted.
    std::array<float, kRegions> lumShown{};
    std::array<float, kRegions> redShown{};
};

// One frame. `in` holds kTiles tiles in row-major order (index = y * cols + x); `in[t].lum`,
// `.red` and `.guardedLum` are quantised here as the GPU's tile pass packs them.
FrameResult step(State& state, const Layout& layout, const std::array<TileInput, kTiles>& in, float dt);

// Events still inside the window.
int eventsInWindow(const Track& track);

}  // namespace geode::viz::flash_limit
