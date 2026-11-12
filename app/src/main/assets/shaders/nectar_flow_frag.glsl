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
//#include lib_dmt

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
// ---- the flight ------------------------------------------------------------
//
// The camera rides lib_dmt's flight path rather than a straight line down -z:
// it banks up, down, left and right through the volume on the path's own
// weave. It used to lean toward the last spike's heading too, which turned
// the flight to the music; owner rule, nothing turns to the music, so the
// path here is lib_dmt's weave without that lean (nfPath), and a spike moves
// the whole path sideways toward its heading instead: a change of place,
// gliding in as uMoveDir does. Position is integrated on uFlowPhase. The volume
// is world-anchored, so the fold moves past the camera as it should, and a
// volume march needs no correction for a curved camera path - there is no
// distance estimate to overshoot.
//
// Behind the volume is the chrysanthemum, evaluated in the flight frame so it
// sits on the axis the camera is flying toward and turns as the flight banks:
// the mandala is what the tunnel of dye is aimed at.
//
// ---- audio ------------------------------------------------------------------
//
// (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) sets the dye's density, sndShaped(SND_LOW) the fold depth, SND_TREBLE_PRESENCE the
// filament sharpness. A spike shifts the flight toward its heading (uMoveDir), re-seeds the dye
// (uSpawnSeed) and moves the fold count to a new plateau (uFormPhase).
//
// ---- knobs ------------------------------------------------------------------
//
// Flame's, Opal's and Hollow's roles, each read as its offset from 0.5 so off
// is the tuned look: k0 PULSE the emission, k1 TEXTURE the filaments' width,
// k2 FLOW added to the stir's bend (fluidWarp3's amount), k3 TONE the palette
// position and the fold's squash (a high melody packs the nectar into finer,
// denser chambers, a low one opens it out; always inside the 1.22..1.42 band
// where the structure holds), k4 BUILD the core's glow, k5 SPACE how deep the slab reaches.

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

/**
 * lib_dmt's flight path (dmtTunnelPath) without its lean on uMoveDir, which
 * grew with depth and so turned the heading on every spike. The heading now
 * shifts the path by a fixed amount instead: the weave is the same, the
 * flight moves over and never turns to the music.
 */
vec2 nfPath(float z) {
    return vec2(1.4 * sin(z * 0.2101554) + 2.6 * sin(z * 0.0429515 + 1.3),
                1.2 * cos(z * 0.1702718 + 0.7) + 2.2 * cos(z * 0.0306796))
         + 0.6 * uMoveDir;
}

/** dmtFlightBasis on nfPath: right, up and forward, forward along the path's tangent. */
mat3 nfFlightBasis(float dist, float roll) {
    vec2 ahead = nfPath(dist + 0.01);
    vec2 behind = nfPath(dist - 0.01);
    vec3 fwd = normalize(vec3((ahead - behind) * 50.0, 1.0));
    vec3 upRef = vec3(sin(roll), cos(roll), 0.0);
    vec3 right = normalize(cross(upRef, fwd));
    vec3 up = cross(fwd, right);
    return mat3(right, up, fwd);
}

void main() {
    vec2 uv = view();
    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float tone = 0.6 * (clamp(uKnob[3], 0.0, 1.0) - 0.5);
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.5);
    float treb = clamp(SND_TREBLE_PRESENCE, 0.0, 1.5);
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float finger = touchFalloff(uv, 0.6);

    // A spike moves the fold's bend to a new plateau: the structure changes
    // species - crystalline, coral, lattice - and holds there.
    float bend = 0.35 + 0.55 * uFormPhase;
    // Kept inside 1.22..1.42. Below it the iteration barely folds and the
    // volume is fog; above it the trap outruns its own scale correction and
    // the structure turns to noise.
    float squash = clamp(1.26 + 0.12 * clamp(bass, 0.0, 1.0) + 0.13 * tone, 1.22, 1.42);
    // TEXTURE widens the filaments (a softer shell), never past half or 1.5 times.
    float sharp = (22.0 + 26.0 * clamp(treb, 0.0, 1.0)) / (1.0 + kTexture);
    float warpScale = 0.95 + 0.30 * swell;
    float warpAmount = max(0.13 + 0.10 * bass + 0.06 * finger + 0.1 * kFlow, 0.0);

    // The flight. Position is INTEGRATED on uFlowPhase - loudness sets how fast
    // the camera travels, never where it is - and the path banks the heading.
    float fly = uFlowPhase * 2.2;
    float roll = uTime * 0.05;
    mat3 flight = nfFlightBasis(fly, roll);
    vec3 ro = vec3(nfPath(fly), fly);
    vec3 rd = flight * normalize(vec3(uv, NF_FOCAL));

    // ---- the volume march ---------------------------------------------------
    //
    // Fixed steps through a slab. Jittered by a per-pixel hash so the step
    // boundaries scatter into grain instead of banding into visible shells,
    // and the jitter walks with the spawn so the grain is not a fixed screen
    // pattern.
    float span = NF_FAR * (1.0 + 0.4 * kSpace) - NF_NEAR;
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
            vec3 emit = pal(fract(0.12 + depth * 0.42 + uFormPhase * 0.25 + d * 0.30 + tone));
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
    // The mandala behind the dye, seen through what the dye did not absorb.
    col += dmtChrysanthemum(transpose(flight) * rd, 0.68 + tone, swell) * trans;

    // The core the flight is aimed at: a small bright sun on the axis, its
    // halo swelling on a spike rather than flashing.
    float axis = pow(max(1.0 - length(uv) * 0.9, 0.0), 6.0);
    col += pal(0.06 + tone) * axis * 0.16 * trans * (1.0 + kBuild);

    // The particle layer, matching the rest of the family.
    col += mix(pal(0.50 + tone), vec3(1.0), 0.35) * fluidMotes(uv * 1.1, 6.0, 0.14) * 0.22;

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.70 + 0.30 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col * (1.0 + 0.5 * kPulse)), 1.0);
}
