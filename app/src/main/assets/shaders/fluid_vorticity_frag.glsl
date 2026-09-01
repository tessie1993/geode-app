#version 300 es
// Ported from WebGL-Fluid-Simulation - MIT License, (c) 2017 Pavel Dobryakov
// (vorticity confinement, GPU Gems ch.38 / Fedkiw 2001)
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity/dye/pressure values far exceed
// that; on GPUs honoring sampler precision (Mali) every read clamped
// and quantized - the on-device "few pixels then black" root cause.
precision highp sampler2D;
in vec2 vUv; in vec2 vL; in vec2 vR; in vec2 vT; in vec2 vB;
uniform sampler2D uVelocity;
uniform sampler2D uCurl;
uniform float uCurlStrength;
uniform float uDx;   // grid cell size: force = eps * h * (N x omega), GPU Gems ch.38
uniform float uDt;
out vec4 fragColor;
void main() {
    float L = texture(uCurl, vL).x;
    float R = texture(uCurl, vR).x;
    float T = texture(uCurl, vT).x;
    float B = texture(uCurl, vB).x;
    float C = texture(uCurl, vUv).x;
    vec2 grad = 0.5 * vec2(abs(T) - abs(B), abs(R) - abs(L));
    vec2 n = grad / (length(grad) + 1e-4);
    // The h (= dx) factor is load-bearing: our curl carries a halfRdx
    // (1/2dx) scale, so omitting dx made the force ~1/dx (64-142x) too
    // strong - velocity exploded, divergence overflowed half-float to Inf,
    // the projection went NaN and the dye rendered black within frames.
    // With eps*h*omega this matches the upstream MIT sim's tuning for the
    // 0..50 curl range exactly.
    vec2 force = uCurlStrength * uDx * C * vec2(n.x, -n.y);
    vec2 v = texture(uVelocity, vUv).xy + force * uDt;
    fragColor = vec4(clamp(v, vec2(-1000.0), vec2(1000.0)), 0.0, 1.0);
}
