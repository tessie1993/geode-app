#version 300 es
// Jacobi pressure solve with alpha = -dx^2 and Neumann boundaries, per
// FLUID_SIM v2 sections 6.2/6.4 (GPU Gems ch.38 formulation).
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity/dye/pressure values far exceed
// that; on GPUs honoring sampler precision (Mali) every read clamped
// and quantized - the on-device "few pixels then black" root cause.
precision highp sampler2D;
in vec2 vUv; in vec2 vL; in vec2 vR; in vec2 vT; in vec2 vB;
uniform sampler2D uPressure;
uniform sampler2D uDivergence;
uniform float uAlpha;   // -dx*dx
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
    float div = texture(uDivergence, vUv).x;
    fragColor = vec4((L + R + B + T + uAlpha * div) * 0.25, 0.0, 0.0, 1.0);
}
