#version 300 es
// Ported from WebGL-Fluid-Simulation - MIT License, (c) 2017 Pavel Dobryakov
// 4-tap cross average used for both the bloom downsample and the additive
// (ONE,ONE) upsample back through the mip chain.
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity/dye/pressure values far exceed
// that; on GPUs honoring sampler precision (Mali) every read clamped
// and quantized - the on-device "few pixels then black" root cause.
precision highp sampler2D;
in vec2 vL;
in vec2 vR;
in vec2 vT;
in vec2 vB;
uniform sampler2D uTexture;
out vec4 fragColor;
void main() {
    vec4 sum = texture(uTexture, vL) + texture(uTexture, vR) +
        texture(uTexture, vT) + texture(uTexture, vB);
    fragColor = sum * 0.25;
}
