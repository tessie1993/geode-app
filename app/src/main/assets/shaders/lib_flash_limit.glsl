// The output flash limiter's shared GLSL (core/viz/FlashLimitPass). Every function here mirrors one
// in core/viz/FlashLimiter.cpp, the host-tested reference; change both together. The constants are
// not repeated: FlashLimitPass uploads safety::kFlash* / kRed* (core/viz/VisualSafety.hpp) as uniforms.

// 16-bit fixed point in two RGBA8 channels (hi, lo), so the state is exact and identical on every
// GPU without a half-float render target. The quarter-step bias lands each byte on its integer
// whether the driver's float-to-unorm conversion rounds or truncates; flUnpack16 rounds it away.
vec2 flPack16(float v) {
    float q = floor(clamp(v, 0.0, 1.0) * 65535.0 + 0.5);
    float hi = floor(q / 256.0);
    return (vec2(hi, q - hi * 256.0) + 0.25) / 255.0;
}

float flUnpack16(vec2 e) {
    vec2 b = floor(e * 255.0 + 0.5);
    return (b.x * 256.0 + b.y) / 65535.0;
}

float flQuantise(float v) { return floor(clamp(v, 0.0, 1.0) * 65535.0 + 0.5) / 65535.0; }

vec3 flSrgbToLinear(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 flLinearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

float flLuminance(vec3 lin) { return dot(lin, vec3(0.2126, 0.7152, 0.0722)); }

uniform float uRedSaturation;
uniform float uRedGuardRatio;

// R / (R + G + B) >= saturation, without the division.
bool flSaturatedRed(vec3 c) { return c.r > 0.0 && c.r * (1.0 - uRedSaturation) >= uRedSaturation * (c.g + c.b); }

// The red guard: a pixel redder than the guard ratio moved toward the grey of its own luminance until
// it sits on that ratio, under the saturation gate: red value 0, luminance unchanged.
vec3 flGuard(vec3 c) {
    float excess = c.r * (1.0 - uRedGuardRatio) - uRedGuardRatio * (c.g + c.b);
    if (!(excess > 0.0)) return c;
    float y = flLuminance(c);
    float s = min(1.0, excess / (excess + (3.0 * uRedGuardRatio - 1.0) * y));
    return c + s * (vec3(y) - c);
}

// flash_limit::measure: (luminance, saturated-red value, luminance once guarded) of a linear pixel.
vec3 flMeasure(vec3 c) {
    float red = flSaturatedRed(c) ? c.r - c.g - c.b : 0.0;
    return vec3(flLuminance(c), red, flLuminance(flGuard(c)));
}

// The pixel a cell (the measuring grid, kCellsPerTile per tile each way) starts at along one axis,
// and the cell a pixel falls in: the same integer split everywhere, so the pixels a tile's transform
// is applied to are exactly the pixels its means were taken over.
int flCellStart(int cell, int size, int cells) { return min((cell * size) / cells, size - 1); }
int flCellOf(int pixel, int size, int cells) { return min(((pixel + 1) * cells - 1) / size, cells - 1); }
