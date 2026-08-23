#version 300 es
// Feedback-trail warp: resample the
// previous frame slightly zoomed + sine-warped and decayed - the MilkDrop
// "warp shader has a memory" liquid-echo effect. Runs with blending OFF:
// this pass IS the new frame base; the scene draws on top.
precision highp float;
in vec2 vUv;
uniform highp sampler2D uPrev;
uniform float uDecay;   // 0..1 kept energy per frame
uniform float uZoom;    // +in / -out per frame (small, e.g. 0.01)
uniform float uWarp;    // sine warp amplitude 0..1
uniform float uTime;
out vec4 fragColor;
void main() {
    vec2 c = vUv - 0.5;
    c *= (1.0 - uZoom * 0.06);
    vec2 uv = c + 0.5;
    if (uWarp > 0.001) {
        uv += vec2(
            sin(uv.y * 11.0 + uTime * 0.7),
            sin(uv.x * 13.0 - uTime * 0.9)
        ) * uWarp * 0.004;
    }
    vec3 prev = texture(uPrev, clamp(uv, vec2(0.001), vec2(0.999))).rgb;
    fragColor = vec4(prev * uDecay, 1.0);
}
