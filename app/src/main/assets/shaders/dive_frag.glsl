#version 300 es
// Endless zoom (core/viz/DivePass, the law in core/viz/EndlessZoom.hpp): the finished picture of any
// style, nested inside itself an octave apart. Every copy only ever grows (or, backing out, shrinks),
// a new one opens at the centre as the outermost passes the frame, so the eye travels through the
// picture without ever arriving. The copies are this frame's picture: nothing is fed back, so the
// style stays live and sharp, and no copy is drawn above kBase octaves of magnification.
//
// endless::tunnel in EndlessZoom.hpp is this file's weights in C++, pinned by viz_math_test: change
// both together.
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;
uniform vec2 uTexel;    // one texel of uScene
uniform float uPhase;   // octaves travelled, 0..1
uniform float uMix;     // 0 the plain picture .. 1 the dive

const int kInner = 5;          // copies inside the frame-filling one
const int kBase = 1;           // copies outside it, under the rounded corners
const float kFeather = 0.22;   // a copy's soft edge, in its own half-frames
const float kShade = 0.45;     // how dark the wall just outside a copy's edge falls
const float kShadeReach = 0.5;

// A rounded frame: 1 on the copy's edge. Round corners read as a shaft, square ones as stacked screens.
float rim(vec2 q) {
    vec2 a = q * q;
    return sqrt(sqrt(dot(a, a)));
}

vec2 fold(vec2 p) { return 1.0 - abs(1.0 - mod(p, 2.0)); }

vec3 copy(vec2 q, float s) {
    vec2 uv = fold(q * 0.5 + 0.5);
    if (s >= 0.8) return texture(uScene, uv).rgb;
    // A shrunk copy covers 1 / s texels a pixel: average them, or it shimmers.
    vec2 o = uTexel * (0.25 / s);
    vec3 c = texture(uScene, uv + o).rgb + texture(uScene, uv - o).rgb +
             texture(uScene, uv + vec2(o.x, -o.y)).rgb + texture(uScene, uv + vec2(-o.x, o.y)).rgb;
    if (s >= 0.3) return c * 0.25;
    o *= 3.0;
    c += texture(uScene, uv + o).rgb + texture(uScene, uv - o).rgb +
         texture(uScene, uv + vec2(o.x, -o.y)).rgb + texture(uScene, uv + vec2(-o.x, o.y)).rgb;
    return c * 0.125;
}

void main() {
    vec2 c = vUv * 2.0 - 1.0;
    vec3 col = vec3(0.0);
    float left = 1.0;
    // Innermost first, so a pixel stops at the first copy that covers it.
    for (int k = kInner; k >= -kBase; k--) {
        float s = exp2(uPhase - float(k));
        vec2 q = c / s;
        float d = rim(q);
        float a = k == -kBase ? 1.0 : 1.0 - smoothstep(1.0, 1.0 + kFeather, d);
        // The newest copy opens over the octave, so the wrap adds and drops nothing visible.
        if (k == kInner) a *= smoothstep(0.0, 1.0, uPhase);
        if (a > 0.0) {
            col += left * a * copy(q, s);
            left *= 1.0 - a;
            if (left < 0.004) break;
        }
        // The wall outside this copy's edge falls into shadow: depth, not a collage.
        float wall = 1.0 - smoothstep(1.0, 1.0 + kShadeReach, d);
        if (k == kInner) wall *= smoothstep(0.0, 1.0, uPhase);
        left *= 1.0 - kShade * wall * step(1.0, d);
    }
    if (uMix < 1.0) col = mix(texture(uScene, vUv).rgb, col, uMix);
    fragColor = vec4(col, 1.0);
}
