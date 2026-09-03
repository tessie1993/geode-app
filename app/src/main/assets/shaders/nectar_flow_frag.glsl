#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_touch

// Nectar Flow: a volume rather than a surface. Luminous dye is wrapped around a
// folded skeleton and then stirred by the same incompressible flow Curl Bloom
// deforms its solid with, and the camera flies through it.
//
// The pairing is the point. A fractal fold gives structure that repeats at
// every scale and has hard, obviously artificial symmetry; an incompressible
// flow gives motion that is smooth, organic and never repeats. Either alone is
// a style you have seen. The fold decides WHERE the light is and the flow
// decides how it MOVES, so the result reads as something built breathing.
//
// ---- why this marches differently ------------------------------------------
//
// There is no distance function here and no surface to hit. The ray takes
// FIXED steps through a bounding slab and accumulates emission and extinction,
// so nothing can overshoot and no Lipschitz correction is needed - the same
// reasoning nebula_frag sets out for its own volume loop. That is also why the
// domain can be folded as violently as it is: a fold that would let rays
// through a surface costs a volume nothing.
//
// ---- audio ------------------------------------------------------------------
//
// uSwell sets the dye's density, uBassSmooth the fold depth, uTrebleSmooth the
// filament sharpness. A spike re-aims the flight (uMoveDir), re-seeds the dye
// (uSpawnSeed) and moves the fold count to a new plateau (uFormPhase). The
// core's flare rides uSpike, which has a rise on it, so it swells.

#define NF_MAX_STEPS 96
#define NF_FOCAL 1.35
#define NF_NEAR 0.30
#define NF_FAR 6.4
#define NF_MIN_TRANSMITTANCE 0.012
#define NF_FOLDS 5

// The skeleton: a fold-and-scale iteration, the 3D cousin of the tapestry in
// blacklight_bloom. The orbit trap - how near the point ever came to the fold
// plane - is what the dye is hung on, so the structure is lines and sheets
// rather than a blob.
//
// `bend` is how far each fold rotates the space before the next one; it is
// what decides whether the result reads as crystalline or as coral.
float skeleton(vec3 p, float bend, float squash) {
    float trap = 1e9;
    float w = 1.0;
    mat3 turn = rotY(bend) * rotX(bend * 0.62);
    for (int i = 0; i < NF_FOLDS; i++) {
        p = abs(p) - 0.55;
        p = turn * p;
        p *= squash;
        w *= squash;
        // Measured back in the original scale, so a deep iteration contributes
        // a thin filament and not a thick slab.
        trap = min(trap, (min(min(abs(p.x), abs(p.y)), abs(p.z)) - 0.06) / w);
    }
    return trap;
}

// Dye density at a world point. The domain is stirred first, so the whole
// structure is being folded through itself while it holds its shape.
float density(vec3 p, float bend, float squash, float sharp, float scale, float amount) {
    vec3 q = fluidWarp3(p, scale, amount);
    float trap = skeleton(q, bend, squash);
    // A thin shell around the trap surface: `sharp` closes it as treble rises,
    // which turns soft nectar into bright filaments.
    return exp(-abs(trap) * sharp);
}

void main() {
    vec2 uv = view();

    float bass = clamp(uBassSmooth, 0.0, 1.5);
    float treb = clamp(uTrebleSmooth, 0.0, 1.5);
    float swell = clamp(uSwell, 0.0, 1.5);
    float finger = touchFalloff(uv, 0.6);

    // A spike moves the fold's bend to a new plateau: the structure changes
    // species - crystalline, coral, lattice - and holds there.
    float bend = 0.35 + 0.55 * uFormPhase;
    // Kept inside 1.22..1.42. Below it the iteration barely folds and the
    // volume is fog; above it the trap outruns its own scale correction and
    // the structure turns to noise.
    float squash = 1.26 + 0.12 * clamp(bass, 0.0, 1.0);
    float sharp = 22.0 + 26.0 * clamp(treb, 0.0, 1.0);
    float warpScale = 0.95 + 0.30 * swell;
    float warpAmount = 0.13 + 0.10 * bass + 0.06 * finger;

    // The flight. Position is INTEGRATED on uFlowPhase - loudness sets how fast
    // the camera travels, never where it is - and a spike banks the heading.
    vec3 ro = vec3(uMoveDir * 0.35, -uFlowPhase * 2.2);
    mat3 cam = rotZ(uTime * 0.05) * rotY(uMoveDir.x * 0.30) * rotX(uMoveDir.y * 0.22);
    vec3 rd = cam * normalize(vec3(uv, NF_FOCAL));

    // ---- the volume march ---------------------------------------------------
    //
    // Fixed steps through a slab. Jittered by a per-pixel hash so the step
    // boundaries scatter into grain instead of banding into visible shells,
    // and the jitter walks with the spawn so the grain is not a fixed screen
    // pattern.
    float span = NF_FAR - NF_NEAR;
    float dt = span / float(NF_MAX_STEPS);
    float jitter = hash13(vec3(uv * 811.0, uSpawnSeed * 37.0));
    float t = NF_NEAR + jitter * dt;

    vec3 acc = vec3(0.0);
    float trans = 1.0;
    for (int i = 0; i < NF_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps * 0.75) break;
        if (trans < NF_MIN_TRANSMITTANCE) break;

        vec3 p = ro + rd * t;
        float d = density(p, bend, squash, sharp, warpScale, warpAmount);
        if (d > 0.004) {
            // Colour by depth into the volume and by how dense the sample is,
            // so the thin outskirts and the bright core are different hues
            // rather than the same hue at two brightnesses.
            float depth = (t - NF_NEAR) / span;
            vec3 emit = pal(fract(0.12 + depth * 0.42 + uFormPhase * 0.25 + d * 0.30));
            // The filament highlight: the top of the density range only.
            emit += vec3(1.0) * smoothstep(0.72, 0.98, d) * (0.25 + 0.7 * treb);
            float sigma = d * (2.6 + 2.2 * swell);
            // Emission-absorption: the standard front-to-back accumulation, so
            // near dye correctly hides far dye instead of summing with it.
            float a = 1.0 - exp(-sigma * dt);
            acc += trans * emit * a * (0.9 + 0.5 * swell);
            trans *= 1.0 - a;
        }
        t += dt;
    }

    vec3 col = acc;
    // The medium, so the empty parts are not flat black.
    col += pal(0.68) * 0.045 * trans * (0.5 + 0.5 * swell);

    // The core the flight is aimed at: a small bright sun on the axis, its
    // halo swelling on a spike rather than flashing.
    float axis = pow(max(1.0 - length(uv) * 0.9, 0.0), 6.0);
    col += pal(0.06) * axis * (0.16 + 0.26 * uSpike) * trans;

    // The particle layer, matching the rest of the family.
    col += mix(pal(0.50), vec3(1.0), 0.35) * fluidMotes(uv * 1.1, 6.0, 0.14) * 0.22;

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.70 + 0.30 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
