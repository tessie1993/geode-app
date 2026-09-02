#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). On GPUs honoring sampler precision (Mali) reads are
// clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uTex;
uniform float uZoom;
uniform float uRotation;
uniform float uZoomPhase;
uniform float uMirrorX;
uniform float uHue;
uniform float uSat;
uniform float uBright;
uniform float uContrast;
uniform float uGamma;
uniform float uInvert;
uniform float uIntensity;
// Palette tint (Color > Palettes). uPalTint is the blend amount and is 0 by
// default, which skips the whole stage: a .milk preset authors its own
// colours, so the palette may only STEER them, never replace them.
uniform float uPalBase;
uniform float uPalSpan;
uniform float uPalTint;

// Chroma below which a pixel has no hue worth steering (see paletteTint).
const float TINT_CHROMA_KNEE = 0.15;

// Chroma a fully tinted grey gains, so the palette shows on white cores.
const float TINT_SAT_LIFT = 0.35;

vec3 rgb2hsv(vec3 c) {
    vec4 k = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, k.wz), vec4(c.gb, k.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    const float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 k = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + k.xyz) * 6.0 - k.www);
    return c.z * mix(k.xxx, clamp(p - k.xxx, 0.0, 1.0), c.y);
}

// Steers the preset's own colours toward the palette (base hue + span) by
// `amount`, in HSV so VALUE is never touched - the preset keeps its structure,
// its contrast and its motion, and only the hues move. A coloured pixel keeps
// its hue RELATIONSHIPS (they are compressed into the palette's band), which
// is what stops every preset from collapsing onto one look; a pixel with no
// chroma has no hue to steer, so it takes the palette entry its own brightness
// selects instead - a gradient map, smooth across a flat white area where a
// steered hue would only amplify quantization noise, and the only way the
// white cores most presets draw can show the palette at all.
vec3 paletteTint(vec3 col, float base, float span, float amount) {
    vec3 hsv = rgb2hsv(col);
    float luma = dot(col, vec3(0.299, 0.587, 0.114));
    // How much of a hue this pixel actually HAS.
    float chroma = smoothstep(0.0, TINT_CHROMA_KNEE, hsv.y);
    float t = mix(luma, hsv.x, chroma);
    float target = base + t * span;
    // Shortest way round the wheel, so a tint never sweeps through the
    // complementary hue on its way to the palette.
    float delta = fract(target - hsv.x + 0.5) - 0.5;
    hsv.x = fract(hsv.x + delta * amount);
    // Only the pixels that had no chroma are given some, and only as far as
    // the blend asks: an already-coloured pixel keeps its saturation EXACTLY,
    // so a tint stays a hue operation and never doubles as a saturation boost.
    hsv.y = mix(hsv.y, hsv.y + (1.0 - hsv.y) * TINT_SAT_LIFT, amount * (1.0 - chroma));
    return hsv2rgb(hsv);
}

vec3 hueRotate(vec3 c, float a) {
    const vec3 w = vec3(0.299, 0.587, 0.114);
    float angle = a * 6.2831;
    float cs = cos(angle);
    float sn = sin(angle);
    return vec3(dot(c, w)) + (c - vec3(dot(c, w))) * cs + cross(vec3(0.57735), c) * sn;
}

void main() {
    vec2 uv = vUv - 0.5;
    if (uMirrorX > 0.5) uv.x = abs(uv.x);
    float a = uRotation;
    uv = mat2(cos(a), -sin(a), sin(a), cos(a)) * uv;
    // Triangle-wave exponent: 1x -> 2x -> 1x smoothly, so the endless-zoom
    // phase wrap never causes a visible scale pop (2^1 snapping to 2^0).
    float z = uZoom * pow(2.0, 1.0 - abs(2.0 * uZoomPhase - 1.0));
    uv = uv / max(z, 0.05) + 0.5;
    vec3 col = texture(uTex, clamp(uv, 0.0, 1.0)).rgb;
    // Palette IDENTITY first, hue ROTATION second - the ownership split the
    // fluid family already runs (FluidHue.kt): "Hue shift" and the colour
    // cycle then turn the tinted frame, instead of being cancelled by it.
    if (uPalTint > 0.001) col = paletteTint(col, uPalBase, uPalSpan, uPalTint);
    col = hueRotate(col, uHue);
    float g = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(g), col, uSat);
    col = (col - 0.5) * uContrast + 0.5;
    col = pow(max(col, 0.0), vec3(1.0 / max(uGamma, 0.05)));
    col *= uBright * uIntensity;
    col = mix(col, max(vec3(1.0) - col, 0.0), uInvert);
    fragColor = vec4(col, 1.0);
}
