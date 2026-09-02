#version 300 es
// Ported from WebGL-Fluid-Simulation - MIT License, (c) 2017 Pavel Dobryakov
// (semi-Lagrangian advection); manual bilerp + per-channel decay per v2 spec.
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity/dye/pressure values far exceed
// that; on GPUs honoring sampler precision (Mali) every read clamped
// and quantized - the on-device "few pixels then black" root cause.
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uVelocity;
uniform sampler2D uSource;
uniform vec2 uSrcInvRes;   // texel size of uSource (bilerp grid)
uniform vec2 uVelInvRes;   // texel size of the VELOCITY grid (back-trace scale)
uniform float uDt;
uniform float uRdx;        // 1/cellSize
uniform vec3 uDecay;       // per-channel (1 + dissipation*dt); velocity uses .xxx
out vec4 fragColor;

vec4 bilerp(sampler2D t, vec2 uv, vec2 inv) {
    vec2 st = uv / inv - 0.5;
    vec2 i = floor(st);
    vec2 f = fract(st);
    vec4 a = texture(t, (i + vec2(0.5, 0.5)) * inv);
    vec4 b = texture(t, (i + vec2(1.5, 0.5)) * inv);
    vec4 c = texture(t, (i + vec2(0.5, 1.5)) * inv);
    vec4 d = texture(t, (i + vec2(1.5, 1.5)) * inv);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec2 vel = texture(uVelocity, vUv).xy;
    // Self-heal: a NaN/Inf velocity texel (driver quirk, transient overflow)
    // must not smear through the back-trace and latch the field black.
    if (isnan(vel.x) || isnan(vel.y) || isinf(vel.x) || isinf(vel.y)) vel = vec2(0.0);
    // The trace displacement is scaled by the VELOCITY grid's texel size, not
    // the source's: with uSrcInvRes here the dye (4x finer grid) would advect
    // at 1/4 of the fluid's actual speed and visibly lag its own vortices.
    vec2 traced = vUv - uDt * uRdx * vel * uVelInvRes;
    vec4 s = bilerp(uSource, traced, uSrcInvRes);
    s.rgb /= uDecay;
    bvec4 nan = isnan(s);
    bvec4 inf = isinf(s);
    if (any(nan) || any(inf)) s = vec4(0.0);
    fragColor = vec4(s.rgb, s.a);
}
