#version 300 es
precision mediump float;

in vec2 vUv;
out vec4 fragColor;

uniform float uFadeAlpha;

void main() {
    // Neutral black: any color bias here accumulates via the per-frame
    // trails fade into a visible tint over the whole image (the old indigo
    // value produced a purple "spectral feedback" wash).
    fragColor = vec4(0.0, 0.0, 0.0, uFadeAlpha);
}
