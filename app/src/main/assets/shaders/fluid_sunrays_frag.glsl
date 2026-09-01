#version 300 es
// Ported from WebGL-Fluid-Simulation - MIT License, (c) 2017 Pavel Dobryakov
// 16-sample radial march from each pixel toward the screen centre (0.5,0.5)
// of the scene-FBO UV space, accumulating the occlusion mask's alpha.
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity/dye/pressure values far exceed
// that; on GPUs honoring sampler precision (Mali) every read clamped
// and quantized - the on-device "few pixels then black" root cause.
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uTexture;
uniform float uWeight;
out vec4 fragColor;
#define ITERATIONS 16
void main() {
    float density = 0.3;
    float decay = 0.95;
    float exposure = 0.7;
    vec2 coord = vUv;
    vec2 dir = vUv - 0.5;
    dir *= 1.0 / float(ITERATIONS) * density;
    float illuminationDecay = 1.0;
    float color = texture(uTexture, vUv).a;
    for (int i = 0; i < ITERATIONS; i++) {
        coord -= dir;
        float col = texture(uTexture, coord).a;
        color += col * illuminationDecay * uWeight;
        illuminationDecay *= decay;
    }
    fragColor = vec4(color * exposure, 0.0, 0.0, 1.0);
}
