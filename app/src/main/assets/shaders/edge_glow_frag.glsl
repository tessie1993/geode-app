#version 300 es
precision highp float;

// The wallpaper's edge look (core/viz/frame/EdgeGlowPass, #439): black over the finished frame, weighted by
// how far inside the screen's rounded rectangle the pixel is: clear on the rim, fully black uWidth pixels
// in, black outside the rounded corners. One screen-covering triangle (fade_vert); the pass blends it with
// SRC_ALPHA over colour and keeps the destination's alpha. The maths is mirrored in
// core/viz/frame/EdgeGlowMath.hpp (blackAt), which core/tests/edge_glow_test.cpp pins: keep the two in step.

out vec4 fragColor;

// The part of the surface on screen in window pixels, GL y-up: (x, y, width, height).
uniform vec4 uWindow;
// The band, from the rim to full black, in pixels; it breathes with the music's level.
uniform float uWidth;
uniform float uRadius;

void main() {
    vec2 halfSize = uWindow.zw * 0.5;
    vec2 p = gl_FragCoord.xy - (uWindow.xy + halfSize);
    vec2 q = abs(p) - (halfSize - vec2(uRadius));
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - uRadius;
    float inside = smoothstep(0.0, uWidth, -d);
    float a = d > 0.0 ? 1.0 : inside * inside * (3.0 - 2.0 * inside);
    fragColor = vec4(0.0, 0.0, 0.0, a);
}
