#version 300 es
// Liquid-ink transport for the WATER style: semi-Lagrangian back-trace of the
// colour film along the SURFACE FLOW, plus dissipation.
//
// Surface water moves down the slope it is standing on, so the transport
// velocity is -grad(h) - the same height gradient water_display_frag already
// shades and refracts with. Advecting the colour by it means the ink runs
// down the flanks of every ripple, spirals around the stirrer wakes and gets
// sucked into the catch-point drains: the film behaves like liquid rather
// than like a texture painted on top of one.
//
// Dissipation is renormalized per frame by RippleMath.inkDissipation, so the
// colour's lifetime does not change with frame rate.
precision highp float;
precision highp sampler2D;
in vec2 vUv; in vec2 vL; in vec2 vR; in vec2 vT; in vec2 vB;
uniform sampler2D uInk;      // rgb = colour, a = coverage
uniform sampler2D uHeight;   // R = height, G = velocity
uniform highp vec2 uInvRes;
uniform float uDt;
uniform float uFlow;         // slope -> uv/s transport gain
uniform float uKeep;         // per-frame survival factor (0..1)
out vec4 fragColor;

float H(vec2 c) {
    vec2 cc = clamp(c, uInvRes * 0.5, 1.0 - uInvRes * 0.5);
    return texture(uHeight, cc).x;
}

void main() {
    vec2 grad = vec2(H(vR) - H(vL), H(vT) - H(vB)) * 0.5;
    // Vertical speed of the surface (the sim's G channel) lifts the film off
    // a rising crest and lets it pool in a falling trough, so a ring reads as
    // water moving rather than as a colour ramp sliding sideways.
    float rise = texture(uHeight, vUv).y;
    vec2 vel = -grad * uFlow * (1.0 + 0.35 * clamp(rise, -2.0, 2.0));
    vec2 src = clamp(vUv - vel * uDt, uInvRes * 0.5, 1.0 - uInvRes * 0.5);
    fragColor = texture(uInk, src) * uKeep;
}
