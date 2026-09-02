#version 300 es
// Capsule (swept-segment) injection with velocity blending, per FLUID_SIM v2
// section 7 (clean-room from spec; distance-to-segment is elementary geometry).
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity/dye/pressure values far exceed
// that; on GPUs honoring sampler precision (Mali) every read clamped
// and quantized - the on-device "few pixels then black" root cause.
precision highp sampler2D;
in vec2 vUv;
in vec2 vSim;
uniform sampler2D uTarget;
uniform vec2 uPrev;       // sim space
uniform vec2 uCur;        // sim space
uniform float uRadius;    // sim space
uniform vec3 uValue;      // mode 0: (targetVel.xy in grid units, 0); mode 1: dye rgb
uniform int uMode;
uniform float uCeiling;   // mode 1: most ink a texel may hold per channel, <=0 unbounded
out vec4 fragColor;

float segDist(vec2 a, vec2 b, vec2 p, out float fp) {
    vec2 ab = b - a;
    float len2 = dot(ab, ab);
    if (len2 < 1e-8) { fp = 0.0; return length(p - a); }
    fp = clamp(dot(p - a, ab) / len2, 0.0, 1.0);
    return length(p - (a + ab * fp));
}

void main() {
    vec4 base = texture(uTarget, vUv);
    float fp;
    float l = segDist(uPrev, uCur, vSim, fp);
    float taper = 1.0 - fp * 0.6;
    float m = exp(-l / max(uRadius, 1e-4)) * taper;
    if (uMode == 0) {
        // Velocity BLENDS toward its target, which is what makes it bounded by
        // the loudest splat no matter how many arrive.
        vec2 v = base.xy + (uValue.xy - base.xy) * m;
        fragColor = vec4(v, 0.0, 1.0);
    } else {
        // Dye ACCUMULATES - two splats crossing are brighter than one, which is
        // the whole point of a dye field - and the decay it is balanced against
        // is a divisor, so a texel settles at injection/(dissipation*dt). That
        // has no upper bound as the dissipation falls, which is how a fade
        // control at its minimum could paint a whole field white.
        //
        // So ink goes into the HEADROOM a texel has left. Far below the ceiling
        // the increment is untouched and splats add exactly as they always did;
        // as the medium fills, the increment fades out, and the min makes the
        // ceiling a guarantee rather than an asymptote even for a single splat
        // larger than the whole headroom. Scaled by the brightest channel and
        // not per channel, so ink that saturates keeps its hue instead of
        // sliding toward white.
        //
        // A ceiling of zero means unbounded, and means it EXACTLY - a caller
        // that grades its dye through a tone map of its own wants the headroom
        // and has to be left alone rather than nearly alone. It is also the
        // right way round for the failure mode: an unset uniform reads zero,
        // and zero here is the behaviour this pass has always had rather than
        // a field clamped to black.
        bool bounded = uCeiling > 0.0;
        float head = bounded ? clamp(1.0 - max(base.r, max(base.g, base.b)) / uCeiling, 0.0, 1.0) : 1.0;
        vec3 ink = base.rgb + uValue * m * head;
        fragColor = vec4(bounded ? min(ink, vec3(uCeiling)) : ink, 1.0);
    }
}
