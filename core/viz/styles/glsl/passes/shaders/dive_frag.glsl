#version 300 es
// Endless zoom (core/viz/DivePass, the law in core/viz/EndlessZoom.hpp): the finished picture of any
// style, nested inside itself. Every copy only ever grows (or, backing out, shrinks), a new one opens
// at the vanishing point as the outermost passes the frame, so the eye travels through the picture
// without ever arriving. The copies are this frame's picture: nothing is fed back, so the style stays
// live and sharp.
//
// No ridges: a copy has no outline, no shadow and no mirrored border. It dissolves into the copy
// around it across its own band, inside its edge, and a shrunk copy's filter widens continuously.
//
// endless::tunnel in EndlessZoom.hpp is this file's weights in C++, pinned by viz_math_test: change
// both together.
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;
uniform vec2 uTexel;    // one texel of uScene
uniform float uAspect;  // width / height
uniform float uPhase;   // copies travelled, 0..1
uniform float uMix;     // 0 the plain picture .. 1 the dive
uniform int uInner;     // copies inside the frame-filling one
uniform int uBase;      // copies outside it: enough to cover the frame
uniform float uStep;    // octaves between two copies
uniform float uTwist;   // radians a copy turns per octave it grows
uniform vec2 uCentre;   // the vanishing point, -1..1 across the frame
uniform float uShape;   // the copy's outline: 2 round .. 8 nearly square
uniform float uBlend;   // the dissolve, as a share of the copy's band
uniform float uFog;     // how far the far end falls into dark
uniform float uLens;    // how much more (or less) of the picture a copy's band holds

// 0 at the vanishing point, 1 on a copy's edge.
float rim(vec2 q) {
    vec2 e = (q - uCentre) / (1.0 - sign(q - uCentre) * uCentre);
    vec2 a = pow(abs(e) + 1e-6, vec2(uShape));
    return pow(a.x + a.y, 1.0 / uShape);
}

vec2 fold(vec2 p) { return 1.0 - abs(1.0 - mod(p, 2.0)); }

vec3 ring(vec2 uv, vec2 o) {
    return 0.25 * (texture(uScene, uv + o).rgb + texture(uScene, uv - o).rgb +
                   texture(uScene, uv + vec2(o.x, -o.y)).rgb + texture(uScene, uv + vec2(-o.x, o.y)).rgb);
}

// A copy shrunk to s covers 1 / s texels a pixel. The footprint grows with that, from nothing at
// full size, so a growing copy sharpens without a step.
vec3 copy(vec2 q, float s) {
    vec2 uv = fold(q * 0.5 + 0.5);
    float spread = 1.0 / s - 1.0;
    if (spread <= 0.0) return texture(uScene, uv).rgb;
    vec2 o = uTexel * 0.25 * min(spread, 12.0);
    vec3 c = ring(uv, o);
    float wide = smoothstep(1.0, 3.0, spread);
    if (wide > 0.0) c = mix(c, 0.5 * (c + ring(uv, o * 3.0)), wide);
    return c;
}

void main() {
    vec2 c = vUv * 2.0 - 1.0;
    vec2 asp = vec2(uAspect, 1.0);
    vec2 v = (c - uCentre) * asp;
    float band = 1.0 - exp2(-uStep);
    float fade = max(uBlend * band, 0.004);
    vec3 col = vec3(0.0);
    float left = 1.0;
    // Innermost first, so a pixel stops at the first copies that cover it.
    for (int k = uInner; k >= -uBase; k--) {
        float octaves = (uPhase - float(k)) * uStep;
        float s = exp2(octaves);
        float turn = uTwist * octaves;
        float cs = cos(turn);
        float sn = sin(turn);
        vec2 q = uCentre + vec2(cs * v.x + sn * v.y, cs * v.y - sn * v.x) / (asp * s);
        float d = rim(q);
        float a = k == -uBase ? 1.0 : 1.0 - smoothstep(1.0 - fade, 1.0, d);
        // The newest copy opens over one step, so the wrap adds and drops nothing visible.
        if (k == uInner) a *= smoothstep(0.0, 1.0, uPhase);
        if (a > 0.0) {
            col += left * a * copy(uLens == 0.0 ? q : uCentre + (q - uCentre) * pow(max(d, 1e-4), uLens), s);
            left *= 1.0 - a;
            if (left < 0.004) break;
        }
    }
    if (uFog > 0.0) col *= pow(clamp(rim(c), 0.0, 1.0), uFog);
    if (uMix < 1.0) col = mix(texture(uScene, vUv).rgb, col, uMix);
    fragColor = vec4(col, 1.0);
}
