#version 300 es
// Ported from WebGL-Fluid-Simulation - MIT License, (c) 2017 Pavel Dobryakov
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity/dye/pressure values far exceed
// that; on GPUs honoring sampler precision (Mali) every read clamped
// and quantized - the on-device "few pixels then black" root cause.
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uTexture;
uniform float uValue;
out vec4 fragColor;
void main() {
    vec4 c = uValue * texture(uTexture, vUv);
    // Pressure is never advected, so a NaN/Inf here would persist forever
    // through the 0.8x warm start - sanitize instead of latching.
    if (any(isnan(c)) || any(isinf(c))) c = vec4(0.0);
    fragColor = c;
}
