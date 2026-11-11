#version 300 es
// Feedback-trail warp: resample the
// previous frame slightly zoomed + sine-warped and decayed - the MilkDrop
// "warp shader has a memory" liquid-echo effect. Runs with blending OFF:
// this pass IS the new frame base; the scene draws on top.
precision highp float;
in vec2 vUv;
uniform highp sampler2D uPrev;
uniform float uDecay;   // 0..1 kept energy per frame
uniform float uScale;   // this frame's resample scale about the centre; below 1 echoes inward
uniform float uWarp;    // sine warp amplitude, scaled to this frame's length
uniform float uTime;
out vec4 fragColor;
void main() {
    vec2 c = vUv - 0.5;
    c *= uScale;
    vec2 uv = c + 0.5;
    if (uWarp > 0.001) {
        uv += vec2(
            sin(uv.y * 11.0 + uTime * 0.7),
            sin(uv.x * 13.0 - uTime * 0.9)
        ) * uWarp * 0.004;
    }
    vec3 prev = texture(uPrev, clamp(uv, vec2(0.001), vec2(0.999))).rgb;
    // An 8-bit target rounds a slow decay back to the same level, so dim trails would never fade:
    // the step below takes every level down by at least one each frame.
    fragColor = vec4(max(prev * uDecay - 0.6 / 255.0, 0.0), 1.0);
}
