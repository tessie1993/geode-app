#version 300 es
// Ported from WebGL-Fluid-Simulation - MIT License, (c) 2017 Pavel Dobryakov
// Final display pass. Compiled as #define keyword variants (SHADING / BLOOM /
// SUNRAYS prepended by FluidLook after the #version line) - never branch on
// uniforms in this hot shader. Drawn with ONE, ONE_MINUS_SRC_ALPHA blending.
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
out vec4 fragColor;

vec3 linearToGamma(vec3 c) {
    c = max(c, vec3(0.0));
    return max(1.055 * pow(c, vec3(0.416666667)) - 0.055, vec3(0.0));
}

void main() {
    vec3 c = texture(uDye, vUv).rgb;

#ifdef SHADING
    vec3 lc = texture(uDye, vL).rgb;
    vec3 rc = texture(uDye, vR).rgb;
    vec3 tc = texture(uDye, vT).rgb;
    vec3 bc = texture(uDye, vB).rgb;
    float dx = length(rc) - length(lc);
    float dy = length(tc) - length(bc);
    vec3 n = normalize(vec3(dx, dy, length(uTexelSize)));
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

    float a = max(c.r, max(c.g, c.b));
    fragColor = vec4(c, a);
}
