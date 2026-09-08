#version 300 es
// Ported from WebGL-Fluid-Simulation - MIT License, (c) 2017 Pavel Dobryakov
// Final display pass. Compiled as #define keyword variants (SHADING / BLOOM /
// SUNRAYS prepended by FluidLook after the #version line) - never branch on
// uniforms in this hot shader. Drawn with ONE, ONE_MINUS_SRC_ALPHA blending.
//
// LOOK, also prepended (always, 0..Look::kLookCount-1), picks the material
// the dye is shown as. Every fluid STYLE is the same solver under a different
// LOOK plus a few multipliers (StyleCatalog's FluidStyle), so the eight looks
// below are the whole of what makes Ink different from Chrome:
//
//   0 raw dye           the original
//   1 ink               pigment on paper: pale ground, dark edged wash
//   2 oil slick         thin-film interference riding density and gradient
//   3 neon              iso-contours of the density, glowing
//   4 chrome            the density gradient as a normal reflecting a studio
//   5 smoke             desaturated, soft, cool
//   6 lava              density as heat: black, red, orange, white
//   7 marble            veins on a pale stone, tinted by the dye
//   8 aurora            the dye's hue turning with time and height, curtained
//
// The looks read uTime, uAudio and uLookHue; LOOK 0 does not, and the linker
// drops them from it.
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity/dye/pressure values far exceed
// that; on GPUs honoring sampler precision (Mali) every read clamped
// and quantized - the on-device "few pixels then black" root cause.
precision highp sampler2D;
in vec2 vUv;
in vec2 vL;
in vec2 vR;
in vec2 vT;
in vec2 vB;
uniform sampler2D uDye;
uniform sampler2D uBloom;
uniform sampler2D uSunrays;
uniform sampler2D uDither;
uniform vec2 uDitherScale;   // target size / dither texture size
uniform vec2 uTexelSize;     // display target texel size (shading normal z)
uniform float uTime;         // scene clock, seconds
uniform vec4 uAudio;         // bass, mid, treble, energy (0..1 after audio drive)
uniform float uLookHue;      // the emitters' base hue, 0..1
out vec4 fragColor;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

/** The dye's own colour at full brightness, so a look can keep the hue and set its own level. */
vec3 tintOf(vec3 c) { return c / max(luma(c), 1e-3); }

/** Cosine ramp around the hue wheel, the same form the fragment styles' palette uses. */
vec3 wheel(float t) { return 0.5 + 0.5 * cos(6.2831 * (t + vec3(0.0, 0.33, 0.67))); }

/** Rotate a colour's hue by `a` turns, in YIQ, where hue is a plain rotation. */
vec3 hueTurn(vec3 c, float a) {
    const mat3 toYiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
    const mat3 fromYiq = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
    vec3 yiq = toYiq * c;
    float r = a * 6.2831;
    yiq.yz = mat2(cos(r), -sin(r), sin(r), cos(r)) * yiq.yz;
    return fromYiq * yiq;
}

vec3 linearToGamma(vec3 c) {
    c = max(c, vec3(0.0));
    return max(1.055 * pow(c, vec3(0.416666667)) - 0.055, vec3(0.0));
}

void main() {
    vec3 c = texture(uDye, vUv).rgb;

    // The density gradient, as a normal. SHADING lights the raw dye with it;
    // the ink, oil-slick, chrome and marble looks need it whether or not shading
    // is on, so it is taken here and the SHADING block below only uses it.
    // On LOOK 0 without SHADING the four taps are dead code and dropped.
#if defined(SHADING) || LOOK == 1 || LOOK == 2 || LOOK == 4 || LOOK == 7
    vec3 lc = texture(uDye, vL).rgb;
    vec3 rc = texture(uDye, vR).rgb;
    vec3 tc = texture(uDye, vT).rgb;
    vec3 bc = texture(uDye, vB).rgb;
    float dx = length(rc) - length(lc);
    float dy = length(tc) - length(bc);
    vec3 n = normalize(vec3(dx, dy, length(uTexelSize)));
    float slope = length(vec2(dx, dy));
#endif

#ifdef SHADING
    float diffuse = clamp(dot(n, vec3(0.0, 0.0, 1.0)) + 0.7, 0.7, 1.0);
    c *= diffuse;
#endif

#ifdef SUNRAYS
    // A dead sunrays target (cleared to 0, or a stalled pass) would multiply
    // the ENTIRE dye to black; clamp so the effect darkens/brightens but can
    // never erase the ink outright.
    float sunrays = clamp(texture(uSunrays, vUv).r, 0.15, 6.0);
    c *= sunrays;
#endif

    // ---- the look ---------------------------------------------------------
    // Applied to the lit, ray-lit dye and before bloom, so the bloom still
    // blooms whatever the look made bright. `a` is the coverage the composite
    // blends with: the raw dye covers where it is present, a look with a
    // ground (ink, marble) covers the whole frame.
    float a = max(c.r, max(c.g, c.b));
#if LOOK == 1
    // Ink on paper. The wash darkens with density and its edges darken more -
    // pigment gathers at the boundary of a wet stroke - and the ground shows
    // through everywhere else.
    float wash = clamp(luma(c) * 1.6, 0.0, 1.0);
    vec3 paper = vec3(0.93, 0.90, 0.84);
    vec3 pigment = mix(vec3(0.05, 0.06, 0.10), tintOf(c) * 0.30, 0.55);
    c = mix(paper, pigment, wash);
    c *= 1.0 - 0.45 * smoothstep(0.0, 0.6, slope) * wash;
    a = 1.0;
#elif LOOK == 2
    // Oil slick. Interference colour indexed by the film's thickness, which
    // is the density, sheared by its gradient so the bands run along the
    // flow; a small specular off the normal reads as the wet surface.
    float lum = luma(c);
    float film = lum * 2.6 + slope * 2.4 + uTime * 0.04 + uLookHue;
    vec3 iri = wheel(film) * (0.35 + 1.3 * lum);
    float spec = pow(clamp(n.z, 0.0, 1.0), 40.0) * smoothstep(0.02, 0.2, lum);
    c = mix(c, iri, 0.75) + vec3(0.35) * spec;
#elif LOOK == 3
    // Neon. Iso-contours of the density: thin bright lines where the density
    // crosses each of seven levels, the dye dim between them. The lines
    // crawl with time so a still field still moves, and bass widens them.
    float lum = luma(c);
    float band = fract(lum * 7.0 - uTime * 0.12);
    float line = 1.0 - smoothstep(0.0, 0.06 + 0.05 * uAudio.x, abs(band - 0.5) - 0.02);
    line *= smoothstep(0.015, 0.08, lum);
    c = c * 0.12 + tintOf(c) * line * (1.1 + 0.8 * uAudio.x);
#elif LOOK == 4
    // Liquid chrome. The density gradient is a surface normal reflecting a
    // studio: dark floor, bright ceiling, a softbox band across the middle.
    // The dye's hue tints the reflection so the chrome is coloured metal.
    float lum = luma(c);
    float upv = n.y * 0.5 + 0.5;
    vec3 env = mix(vec3(0.04, 0.04, 0.07), vec3(0.85, 0.92, 1.0), smoothstep(0.3, 0.7, upv));
    env += vec3(1.0) * pow(max(1.0 - abs(n.y - 0.2) / 0.28, 0.0), 4.0) * 1.1;
    env += vec3(0.4, 0.5, 0.7) * pow(clamp(n.z, 0.0, 1.0), 12.0) * 0.5;
    c = env * mix(vec3(1.0), tintOf(c), 0.55) * smoothstep(0.0, 0.22, lum) * (0.8 + 0.4 * uAudio.w);
    a = max(c.r, max(c.g, c.b));
#elif LOOK == 5
    // Smoke. Most of the colour drained, a cool cast, and a lifted gamma so
    // the thin edges of each plume stay visible.
    float lum = luma(c);
    c = mix(vec3(lum), c, 0.18) * vec3(0.82, 0.88, 1.0);
    c = pow(max(c, vec3(0.0)), vec3(0.8));
#elif LOOK == 6
    // Lava. Density as temperature on a blackbody-shaped ramp: black through
    // red and orange to white, so the hot core of every splat is white and
    // the skirts glow red. Energy raises the whole temperature.
    float heat = clamp(luma(c) * (1.3 + 0.5 * uAudio.w), 0.0, 1.0);
    c = vec3(smoothstep(0.0, 0.42, heat),
             smoothstep(0.28, 0.80, heat) * 0.85,
             smoothstep(0.66, 1.0, heat) * 0.70);
    a = max(c.r, max(c.g, c.b));
#elif LOOK == 7
    // Marble. Veins where the density crosses a fine ladder of levels, drawn
    // on a pale stone and tinted by the dye, and the gradient shears the
    // ladder so the veins follow the flow rather than the levels.
    float lum = luma(c);
    float vein = smoothstep(0.72, 1.0, abs(sin(lum * 18.0 + slope * 7.0 + uLookHue * 6.2831)));
    vein *= smoothstep(0.015, 0.25, lum);
    vec3 stone = vec3(0.84, 0.82, 0.79) * (0.92 + 0.08 * lum);
    c = mix(stone, tintOf(c) * 0.42, vein);
    a = 1.0;
#elif LOOK == 8
    // Aurora. The dye's hue turns with time and with height, so a plume is a
    // different colour at its top than at its root, and vertical curtains of
    // brightness shimmer across it on the treble.
    float lum = luma(c);
    c = hueTurn(c, uTime * 0.03 + vUv.y * 0.45);
    c *= 1.0 + 0.30 * sin(vUv.x * 36.0 + uTime * 1.7) * lum * (0.5 + uAudio.z);
    c = max(c, vec3(0.0));
#endif

#ifdef BLOOM
    vec3 bloom = texture(uBloom, vUv).rgb;
#ifdef SUNRAYS
    bloom *= sunrays;
#endif
    float noise = texture(uDither, vUv * uDitherScale).r;
    noise = noise * 2.0 - 1.0;
    bloom += noise / 255.0;
    bloom = linearToGamma(bloom);
    c += bloom;
#endif

    fragColor = vec4(c, a);
}
