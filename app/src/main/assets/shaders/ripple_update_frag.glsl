#version 300 es
// Velocity-form heightfield wave update with damping, per RippleMath's
// headless-gate-verified CPU mirror (lockstep - change both together):
//   v += c^2*dt*laplacian(h)/dx^2;  v *= damping;  h = (h + v*dt) * heightDecay
// uK packs c^2*dt/dx^2. Clamped-edge boundary via the same half-texel
// clamp idiom as fluid_pressure_frag (Neumann: edge reflects, no wrap).
//
// uHeightDecay is not cosmetic. WITHOUT it the surface only ever GAINS: the
// Laplacian sums to zero over a Neumann grid, so the wave step conserves the
// mean of h exactly, every drop adds positive volume, and nothing takes any
// back. Velocity damping drains the ripples' oscillation but leaves that
// accumulated offset behind, so a pool under a full track climbs until it
// pins against the +/-8 rail, the gradient flattens and the style freezes
// into a saturated sheet - the "it only ever adds more, it never removes
// them" report. Draining h itself is what gives a drop a LIFETIME.
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float height/velocity values exceed that; on GPUs
// honoring sampler precision (Mali) every read clamped and quantized.
precision highp sampler2D;
in vec2 vUv; in vec2 vL; in vec2 vR; in vec2 vT; in vec2 vB;
uniform sampler2D uHeight;   // R = height, G = velocity
uniform float uK;            // c^2 * dt / dx^2
uniform float uDt;
uniform float uDamping;      // per-substep velocity decay factor
uniform float uHeightDecay;  // per-substep height decay (drains the mean)
uniform highp vec2 uInvRes;
out vec4 fragColor;

float sampleH(vec2 c) {
    vec2 cc = clamp(c, uInvRes * 0.5, 1.0 - uInvRes * 0.5);
    return texture(uHeight, cc).x;
}

void main() {
    vec2 hv = texture(uHeight, vUv).xy;
    float lap = sampleH(vL) + sampleH(vR) + sampleH(vT) + sampleH(vB) - 4.0 * hv.x;
    float v = (hv.y + uK * lap) * uDamping;
    // MAX_HEIGHT rail (mirrored in RippleMath.waveStep): a burst of drops can
    // still out-pace the decay for a moment; never let half-floats blow up.
    float h = clamp((hv.x + v * uDt) * uHeightDecay, -8.0, 8.0);
    fragColor = vec4(h, v, 0.0, 1.0);
}
