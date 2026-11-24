#version 300 es
// Ported from WebGL-Fluid-Simulation - MIT License, (c) 2017 Pavel Dobryakov
// Separable 3-fetch blur (offsets +-1.33333 texels = a 5-tap Gaussian);
// uDirection selects the horizontal or vertical pass.
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity/dye/pressure values far exceed
// that; on GPUs honoring sampler precision (Mali) every read clamped
// and quantized - the on-device "few pixels then black" root cause.
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uTexture;
uniform vec2 uDirection;   // (1.33333/w, 0) or (0, 1.33333/h)
out vec4 fragColor;
void main() {
    vec4 sum = texture(uTexture, vUv) * 0.29411764;
    sum += texture(uTexture, vUv - uDirection) * 0.35294117;
    sum += texture(uTexture, vUv + uDirection) * 0.35294117;
    fragColor = sum;
}
