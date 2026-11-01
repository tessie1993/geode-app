#pragma once

// The output flash limiter's per-tile maths, with no GL: the reference the GPU pass
// (viz/FlashLimitPass, shaders/lib_flash_limit.glsl) mirrors operation for operation, and what
// core/tests/flash_limit_test.cpp proves the "three flashes a second" promise against.
//
// The budget and its arithmetic are in VisualSafety.hpp (safety::kFlash*). The state the GPU keeps
// per tile goes through 16-bit fixed point every frame (two RGBA8 channels per value, see
// FlashLimitPass.hpp for why not half floats), so the maths here quantises the same way and the host
// test runs on exactly the values the device will hold.
namespace geode::viz::flash_limit {

constexpr float kQuantSteps = 65535.0f;

// Round to the nearest 16-bit step, as pack16 in lib_flash_limit.glsl.
float quantise(float v);
// Round down to a 16-bit step (the budget: never bank a fraction of a step that was not refilled).
float quantiseDown(float v);

// sRGB transfer functions (IEC 61966-2-1) and Rec.709 relative luminance of linear RGB.
float srgbToLinear(float c);
float linearToSrgb(float c);
float luminance(float r, float g, float b);

struct TileState {
    // Luminance the tile shows (0..1, linear), the band anchor, the bucket (0..capacity).
    float shown = 0.0f;
    float anchor = 0.0f;
    float budget = 0.0f;
    // False until the first frame has been measured; that frame is shown as it is (no fade-in from
    // black after a surface is created or resized).
    bool primed = false;
};

// One frame of one tile: `raw` is the tile's measured mean luminance this frame (it is quantised
// here, as the GPU's tile pass packs it), `dt` the frame step in seconds.
TileState step(const TileState& previous, float raw, float dt);

// What the final pass does to a tile's pixels so its mean luminance becomes `shown`: in linear
// light, out = mix(in * gain, white, lift). Dimming is a pure gain (exact, never clips); raising is
// a blend toward white (exact, never clips: a gain above 1 would clip highlights and fall short).
struct TileGain {
    float gain = 1.0f;
    float lift = 0.0f;
};
TileGain gainFor(float shown, float raw);
// The same on one linear channel value, for tests.
inline float applyGain(const TileGain& g, float linear) { return linear * g.gain + (1.0f - linear * g.gain) * g.lift; }

}  // namespace geode::viz::flash_limit
