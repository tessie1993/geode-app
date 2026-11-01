// The output flash limiter's shared GLSL (core/viz/FlashLimitPass). Every function here mirrors one
// in core/viz/FlashLimiter.cpp, the host-tested reference; change both together. The constants are
// not repeated: FlashLimitPass uploads safety::kFlash* (core/viz/VisualSafety.hpp) as uniforms.

// 16-bit fixed point in two RGBA8 channels (hi, lo), so the state is exact and identical on every
// GPU without a half-float render target. The quarter-step bias lands each byte on its integer
// whether the driver's float-to-unorm conversion rounds or truncates; flUnpack16 rounds it away.
vec2 flPack16(float v) {
    float q = floor(clamp(v, 0.0, 1.0) * 65535.0 + 0.5);
    float hi = floor(q / 256.0);
    return (vec2(hi, q - hi * 256.0) + 0.25) / 255.0;
}

// Rounds down: the bucket never banks a step it was not refilled.
vec2 flPack16Down(float v) {
    float q = floor(clamp(v, 0.0, 1.0) * 65535.0);
    float hi = floor(q / 256.0);
    return (vec2(hi, q - hi * 256.0) + 0.25) / 255.0;
}

float flUnpack16(vec2 e) {
    vec2 b = floor(e * 255.0 + 0.5);
    return (b.x * 256.0 + b.y) / 65535.0;
}

vec3 flSrgbToLinear(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 flLinearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// Rec.709 relative luminance of display-encoded (sRGB) colour.
float flLuminance(vec3 encoded) {
    return dot(flSrgbToLinear(encoded), vec3(0.2126, 0.7152, 0.0722));
}

uniform float uFlBand;
uniform float uFlCapacity;
uniform float uFlRefill;
uniform float uFlFree;
uniform float uFlMaxDt;

// flash_limit::step: returns (shown, anchor, budget). `raw` is already quantised (the tile pass
// packed it). See VisualSafety.hpp for why |anchor - shown| <= band makes the budget a flash count.
vec3 flStep(float shown, float anchor, float budget, bool primed, float raw, float dt) {
    if (!primed) return vec3(raw, raw, uFlCapacity);
    float h = uFlBand;
    float stepDt = clamp(dt, 0.0, uFlMaxDt);
    float bucket = min(uFlCapacity, budget + uFlRefill * stepDt);
    float freeMove = uFlFree * stepDt;
    float up = max(anchor + h - shown, 0.0) + freeMove + bucket;
    float down = max(shown - anchor + h, 0.0) + freeMove + bucket;
    float moved = clamp(shown + clamp(raw - shown, -down, up), 0.0, 1.0);
    float dragged = clamp(anchor, moved - h, moved + h);
    float spent = max(abs(dragged - anchor) - freeMove, 0.0);
    return vec3(moved, dragged, max(bucket - spent, 0.0));
}

// flash_limit::gainFor: (gain, lift) so mix(linear * gain, white, lift) has mean luminance `shown`.
vec2 flGain(float shown, float raw) {
    if (shown < raw) return vec2(shown / raw, 0.0);
    if (shown > raw) return vec2(1.0, (shown - raw) / (1.0 - raw));
    return vec2(1.0, 0.0);
}
