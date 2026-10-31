#version 300 es
precision highp float;

// LIFE - the present pass. The state is a field of living matter (Lenia A or
// Gray-Scott v), an age trace and the melody ink each cell was born with.
// lib_relief reads the field as a HEIGHT: a parallax march under a view that
// rocks on the sound clocks, a light that orbits on the mid clock and climbs
// with the melody, and a material per cell. Each style has two materials
// (LifeScene's look table): young matter wears one, old matter (the soma, by
// the age trace) the other, and every hard cut swaps them, so a section
// change re-lights the whole colony the moment the music turns.
//
// Colour: the palette's hue moved by the melody ink, so colonies carry the
// colour of the line that grew them; new births glow at the complementary
// hue (Synapse's sparks), flaring on note starts. Brightness follows only the
// energy trend, gently.
//
// The pass reads the sim at its own resolution with linear filtering, so the
// organism keeps its softness at any screen size. Cost: lib_relief's march
// (10..24 height reads) plus five for the normal and one for the material.

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uState;
uniform vec2 uRes;        // output size
uniform vec2 uSimRes;     // state size
uniform vec2 uFrac;       // drift not yet shifted into the state, texels
uniform int uLookYoung;   // RELIEF_* for young matter (and old after an odd number of hard cuts)
uniform int uLookOld;     // RELIEF_* for old matter
uniform float uDepth;     // relief thickness, uv
uniform float uShowV;     // 0 = show A/u, 1 = show v (the GS pattern channel)
uniform float uBaseHue;
uniform float uHueSpan;

//#include lib_sound

float liveAt(vec2 uv) {
    vec4 s = texture(uState, uv);
    return mix(s.r, s.g, uShowV);
}

// Gray-Scott's v lives around 0..0.45, Lenia's A in 0..1: both become a 0..1 height.
float reliefHeight(vec2 uv) {
    float v = liveAt(uv);
    return uShowV > 0.5 ? smoothstep(0.03, 0.42, v) : smoothstep(0.02, 0.92, v);
}

// About one sim texel: finer than that reads the bilinear grid as facets.
#define RELIEF_EPS (1.2 / min(uSimRes.x, uSimRes.y))
//#include lib_relief

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

void main() {
    vec2 uv = vUv - uFrac / uSimRes;
    Relief r = reliefMarch(uv, reliefTilt(), uDepth);
    vec4 s = texture(uState, r.uv);
    float live = mix(s.r, s.g, uShowV);
    float age = s.b;
    float ink = s.a;

    float hue = uBaseHue + uHueSpan * (0.38 * ink + 0.1 * age + 0.08 * r.h);
    float sat = 0.78 - 0.2 * smoothstep(0.85, 1.0, r.h);
    vec3 albedo = hsv2rgb(vec3(fract(hue), sat, mix(0.35, 1.0, r.h)));

    vec3 light = reliefLight();
    float sparkle = rlSat(SND_HAT_ENV * 1.2 + 0.3 * SND_TREBLE_PRESENCE);
    int young = reliefLook(uLookYoung, uLookOld);
    int old = reliefLook(uLookOld, uLookYoung);
    vec3 col = reliefShade(young, albedo, r, light, sparkle);
    // Old, thick matter is the soma: the second material, like Synapse's core under its membrane.
    float soma = smoothstep(0.5, 0.95, age) * smoothstep(0.3, 0.7, r.h);
    // Shaded unconditionally: terra and ink take fwidth, which needs uniform control flow.
    col = mix(col, reliefShade(old, albedo, r, light, sparkle), soma);

    // Births: matter that is alive but young glows at the complementary hue, flaring on note starts.
    float fresh = smoothstep(0.1, 0.5, live) * (1.0 - smoothstep(0.05, 0.45, age));
    col += hsv2rgb(vec3(fract(hue + 0.45), 0.7, 1.0)) * fresh * (0.15 + 0.55 * rlSat(SND_ONSET_ENV));

    // Depth: the floor sinks into the void, except on paper, where the floor IS the page.
    bool paper = young == RELIEF_INK || (old == RELIEF_INK && soma > 0.5);
    if (!paper) col *= mix(0.4, 1.0, smoothstep(0.0, 0.35, r.h));

    col *= 0.88 + 0.24 * rlSat(SND_ENERGY_TREND);

    float aspect = uRes.x / uRes.y;
    vec2 q = (vUv - 0.5) * vec2(aspect, 1.0);
    col *= 1.0 - 0.3 * dot(q, q);

    fragColor = vec4(max(col, vec3(0.0)), 1.0);
}
