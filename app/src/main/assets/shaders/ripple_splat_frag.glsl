#version 300 es
// Ripple drop injection: additive Gaussian bumps onto the height channel
// (R), velocity channel (G) passed through. Batched: up to 8 drops per pass
// via uDrops (x, y in sim space, z = radius, w = amplitude). Kept in
// lockstep with RippleMath.dropProfile (headless-gate-verified CPU mirror).
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float height/velocity values exceed that; on GPUs
// honoring sampler precision (Mali) every read clamped and quantized.
precision highp sampler2D;
in vec2 vUv;
in vec2 vSim;
uniform sampler2D uTarget;
uniform vec4 uDrops[8];
uniform int uDropCount;
out vec4 fragColor;

void main() {
    vec2 hv = texture(uTarget, vUv).xy;
    for (int i = 0; i < 8; i++) {
        if (i >= uDropCount) break;
        vec4 d = uDrops[i];
        float r = max(d.z, 1e-4);
        vec2 diff = vSim - d.xy;
        // Lockstep with RippleMath.dropProfile: amp * exp(-dist^2 / r^2).
        hv.x += d.w * exp(-dot(diff, diff) / (r * r));
    }
    fragColor = vec4(hv, 0.0, 1.0);
}
