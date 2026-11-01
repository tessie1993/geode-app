#version 300 es
// Every splat of a frame in ONE pass. The capsule maths is fluid_splat_frag's
// (which stays the single-splat template the Customize injection editors start
// from); what changed is that the splats arrive as uniform arrays and are
// applied in a loop, in the order the emitters queued them.
//
// Why: each splat used to be its own full-grid pass, on the velocity AND on
// the dye. With sixteen in a busy frame that was thirty-two full-screen
// passes, at Ultra two million dye texels each - more fill than the whole rest
// of the solver. The loop gives the same answer (velocity blends toward each
// target in turn, dye accumulates into the headroom in turn, exactly as the
// serial passes composed them) for one read and one write per texel.
//
// A splat is skipped past kSplatCutoffRadii (FluidMath.hpp) radii, where its
// falloff is under 1e-5, so a texel far from every splat costs sixteen
// distance tests and no exp().
//
// uMode 0 velocity, 1 dye, 2 temperature (the R channel; heat goes into the
// headroom under the ceiling the same way dye does).
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP; half-float fields need highp.
precision highp sampler2D;
in vec2 vUv;
in vec2 vSim;
uniform sampler2D uTarget;
uniform vec4 uSeg[16];     // (prev.xy, cur.xy), sim space
uniform vec4 uVal[16];     // (radius, value.xyz): velocity in grid units, or dye rgb
uniform int uCount;
uniform int uMode;
uniform float uCeiling;    // mode 1: most ink a texel may hold per channel, <=0 unbounded
uniform float uHeat;       // mode 2: heat at a splat's centre this frame
uniform float uMaxHeat;    // mode 2: the temperature ceiling
out vec4 fragColor;

const float CUTOFF = 11.5;

// FluidMath splatFalloff: exp(-d/r), tapered toward the head.
float falloff(vec4 seg, float radius) {
    vec2 a = seg.xy;
    vec2 ab = seg.zw - a;
    float len2 = dot(ab, ab);
    float fp = len2 < 1e-8 ? 0.0 : clamp(dot(vSim - a, ab) / len2, 0.0, 1.0);
    float d = length(vSim - (a + ab * fp));
    float r = max(radius, 1e-4);
    if (d > r * CUTOFF) return 0.0;
    return exp(-d / r) * (1.0 - fp * 0.6);
}

void main() {
    vec4 base = texture(uTarget, vUv);
    bool bounded = uCeiling > 0.0;
    for (int i = 0; i < 16; i++) {
        if (i >= uCount) break;
        float m = falloff(uSeg[i], uVal[i].x);
        if (m <= 0.0) continue;
        if (uMode == 0) {
            // Velocity BLENDS toward its target, bounded by the loudest splat.
            base.xy += (uVal[i].yz - base.xy) * m;
        } else if (uMode == 1) {
            // Dye accumulates into the headroom the brightest channel leaves.
            float head = bounded ? clamp(1.0 - max(base.r, max(base.g, base.b)) / uCeiling, 0.0, 1.0) : 1.0;
            base.rgb += uVal[i].yzw * m * head;
            if (bounded) base.rgb = min(base.rgb, vec3(uCeiling));
        } else {
            float head = clamp(1.0 - base.r / uMaxHeat, 0.0, 1.0);
            base.r += uHeat * m * head;
        }
    }
    if (uMode == 0) {
        fragColor = vec4(base.xy, 0.0, 1.0);
    } else {
        fragColor = vec4(base.rgb, 1.0);
    }
}
