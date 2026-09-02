#version 300 es
// Ported from WebGL-Fluid-Simulation - MIT License, (c) 2017 Pavel Dobryakov
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity/dye/pressure values far exceed
// that; on GPUs honoring sampler precision (Mali) every read clamped
// and quantized - the on-device "few pixels then black" root cause.
precision highp sampler2D;
in vec2 vUv; in vec2 vL; in vec2 vR; in vec2 vT; in vec2 vB;
uniform sampler2D uPressure;
uniform sampler2D uVelocity;
uniform float uHalfRdx;
uniform highp vec2 uInvRes;
out vec4 fragColor;
float sampleP(vec2 c) {
    vec2 cc = clamp(c, uInvRes * 0.5, 1.0 - uInvRes * 0.5);
    return texture(uPressure, cc).x;
}
void main() {
    float L = sampleP(vL);
    float R = sampleP(vR);
    float T = sampleP(vT);
    float B = sampleP(vB);
    vec2 v = texture(uVelocity, vUv).xy - uHalfRdx * vec2(R - L, T - B);
    // Terminal-speed soft cap (12 sim units/s = 6 screen heights/s): the
    // confinement force injects energy faster than dissipation removes it,
    // so uncapped speed grows without bound and eventually advects the dye
    // off-grid faster than the emitters inject it - the screen fades to
    // black within seconds and the FlowField warp saturates into flashing.
    // A smooth rescale (not a per-axis clamp) preserves flow direction.
    float sp = length(v);
    v *= 12.0 / max(12.0, sp);
    fragColor = vec4(v, 0.0, 1.0);
}
