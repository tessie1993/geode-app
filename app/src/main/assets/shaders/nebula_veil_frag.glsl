#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_nebula

// Nebula Veil: flying along the rim of an old supernova remnant, where the
// shock front lies edge-on to us and the gas is a lace of thin, twisted
// filaments. The flat style drew that lace as ridged noise on the screen;
// here it is a real shell of gas in a volume, and the threads are what a
// thin sheet of glowing gas looks like when you see it edge-on.
//
// ---- the scene ----------------------------------------------------------------
//
// The remnant's centre is an axis parallel to the flight, off to the lower
// left (VV_C), so the front is a broad cylindrical shell that arcs across the
// upper right of the frame and runs away to the vanishing point. Inside the
// shell's band, the gas sits on the iso-sheets of one 3D fbm (nebFbm3),
// squeezed across the front so the sheets lie mostly along it, as a shock
// lays them down. A volume integral of a thin emitting sheet is faint where
// the ray crosses it face-on and bright where the ray runs along it, so the
// folds of the sheets light up as long threads: limb brightening, the reason
// the real Veil is a lace and not a haze. Two strands come from the same
// noise at two iso-levels, a warm palette hue on the outer side of the front
// and a cool one on the inner side, the way the hot and cooler gas separate
// across a real shock. A faint diffuse glow fills the band, a glow sits
// toward the remnant's centre and stars lie behind it all.
//
// ---- camera -------------------------------------------------------------------
//
// camFly: the eye rides the rig's path down the front, so the lace streams
// past one side and arcs over. The eye's z is rebased by VV_ZWRAP (512).
// Every z term has a period dividing it: the noise samples z at exactly 1/2,
// so 512 world units are 256 lattice cells, NEB_Z_WRAP (lib_nebula), and its
// octaves double exactly; the front's folds and the Chaos swirl turn a whole
// number of times per 512 (VV_ZF times an integer). 512 divides the rig's
// 8192 travel wrap, so neither the rebase nor the wrap ever shows. A bubble
// around the eye fades the gas so the camera never flies blind into a sheet.
//
// ---- audio --------------------------------------------------------------------
//
// Highs (sndShaped(SND_HIGH)) LIGHT THE FILAMENTS, as in the flat style: they
//                       thin the sheets and brighten them, so the threads
//                       sharpen and glitter; the diffuse glow and the dark
//                       between them stay where they are.
// Hats (SND_HAT_ENV)    twinkle the stars.
// Kick (SND_KICK_ENV)   sends a shock ripple down the front: a bulge that
//                       leaves just ahead of the eye on the hit and runs away
//                       as the envelope falls. It moves the gas, never its light.
// Key (uKeyHue)         turns both strand hues (nebKeyShift).
// SND_ENERGY_TREND      overall level, gently.
//
// ---- macros and knobs ---------------------------------------------------------
//
// Speed: the gas drifts along the front on nebFlow and churns on nebTime
// (the flight is the rig's). Complexity: uMorph adds noise octaves
// (nebVolOctaves, at most four). Chaos: uWarp/uTurbulence swirl the gas
// (vvWarp, the family's divergence-free swirl made periodic in z); uTwist via
// view(). Colour: both strands and the glows are pal(). Energy: uIntensity
// scales how much the highs light the threads and the kick's ripple. Detail:
// uSteps bounds the march and the step grows when it is low (nebVolDt).
// Knobs, as offsets from 0.5 so off is the tuned look: k0 PULSE the kick
// ripple, k1 TEXTURE the sheet thickness, k2 FLOW how deeply the front folds,
// k3 TONE the strand hues, k4 BUILD the diffuse glow, k5 SPACE how far the
// front stands from the flight path.
//
// ---- why the march is safe ----------------------------------------------------
//
// This is a volume march, not a sphere trace: inside the band it takes
// bounded steps (nebVolDt, dithered), at most NEB_VOL_MAX of them and fewer on
// uSteps. Outside the band it skips: |length(p.xy - C) - R0| is the exact
// distance to the front's rest cylinder (1-Lipschitz), and no gas lies
// farther from it than the band's reach plus VV_WOB, a bound on every fold
// and the ripple at their largest knob settings, so a skip by that gap can
// never jump over gas. Every emission term is bounded and the opacity of a
// step is exact for any length (lib_nebula), so a coarse budget draws the
// lace coarser, never brighter. No atan is taken: the along-front coordinate
// is linear, so there is no seam.

#define VV_FAR 24.0
/** Rebase period of the eye's z; divides 8192 and is 256 noise cells at z scale 1/2. */
#define VV_ZWRAP 512.0
/** TAU / 512: one turn per rebase period; multiply by an integer. */
#define VV_ZF 0.012271846
/** Half-width (1/e) of the front's band of gas. */
#define VV_W 1.1
/** The band's reach: past this the gas is under exp(-4.8). */
#define VV_REACH 2.4
/** Bound on |frontShift|: folds 0.5 * 1.8 plus the ripple's 0.3. */
#define VV_WOB 1.2

/** The remnant's centre, world xy: lower left of the path. */
const vec2 VV_C = vec2(-7.0, -5.5);
/** Unit tangent of the front where it passes the path: perpendicular to -VV_C. */
const vec2 VV_TAN = vec2(-0.6178, 0.7863);

float gT;
float gR0;
float gFold;
float gRipZ;
float gRipA;

/**
 * The front's offset from its rest radius at along-front `v` and rebased z:
 * slow folds, plus the kick's ripple pulling it toward the centre. Bounded by
 * VV_WOB at the largest knob settings.
 */
float frontShift(float v, float z) {
    float f = gFold * (0.32 * sin(z * VV_ZF * 20.0 + v * 0.41) + 0.18 * sin(v * 0.93 - z * VV_ZF * 44.0 + 1.7));
    float dz = z - gRipZ;
    return f - gRipA * exp(-dz * dz * 0.08);
}

/**
 * Chaos as a swirl of the noise coordinates: each component depends only on
 * the other two, so it is divergence-free like nebWarp3, and its z terms turn
 * a whole number of times per VV_ZWRAP, so it keeps the rebase invisible.
 * Skipped at the default.
 */
vec3 vvWarp(vec3 p, float t) {
    float k = nebChaos();
    if (k <= 1e-4) return p;
    float zq = p.z * VV_ZF;
    return p + k * 0.3 * vec3(sin(p.y * 1.3 + t * 0.07) + cos(zq * 90.0),
                              sin(zq * 98.0 + t * 0.05) + cos(p.x * 1.4),
                              0.5 * (sin(p.x * 1.1) + cos(p.y * 1.3 + t * 0.06)));
}

void main() {
    vec2 uv = view();
    gT = nebTime();
    float flow = nebFlow();
    float e = nebEnergy();
    float hi = clamp(sndShaped(SND_HIGH), 0.0, 1.3) * e;
    float hiN = clamp(hi, 0.0, 1.0);
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float oct = nebVolOctaves(3.0);
    float key = nebKeyShift(0.1);

    float kPulse = clamp(1.0 + 2.0 * (uKnob[0] - 0.5), 0.0, 2.0);
    float kTex = clamp(1.0 + 1.2 * (uKnob[1] - 0.5), 0.4, 1.6);
    gFold = clamp(1.0 + 1.6 * (uKnob[2] - 0.5), 0.2, 1.8);
    float tone = 0.2 * (uKnob[3] - 0.5);
    float kBuild = clamp(1.0 + 2.0 * (uKnob[4] - 0.5), 0.0, 2.0);
    gR0 = length(VV_C) + 2.4 + 2.0 * (uKnob[5] - 0.5);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term below has a period dividing VV_ZWRAP (see the header).
    ro.z = mod(ro.z, VV_ZWRAP);

    // The kick's ripple: born just ahead of the eye, running away as it fades.
    gRipZ = ro.z + 2.0 + 16.0 * (1.0 - kick);
    gRipA = min(0.3, 0.22 * kick * kPulse * min(e, 1.5));

    // Gas drifts along the front on the flow clock; it churns along z slowly.
    float drift = flow * 0.01 + gT * 0.008;
    float churn = gT * 0.15;

    // Highs thin the sheets (sharper threads) and brighten them.
    float w0 = mix(0.06, 0.036, hiN) * kTex;
    float gain = (0.55 + 0.75 * hi) * mix(1.0, 1.35, hiN);

    vec3 warm = nebEmission(0.02 + key + tone, 0.15);
    vec3 cool = nebEmission(0.52 + key + tone, 0.15);
    vec3 haze = mix(warm, cool, 0.5);

    vec4 acc = vec4(0.0, 0.0, 0.0, 1.0);
    float tt = 0.1 + nebDither() * 0.1;
    for (int i = 0; i < NEB_VOL_MAX; i++) {
        if (float(i) >= uSteps) break;
        if (tt > VV_FAR) break;
        vec3 p = ro + rd * tt;
        float dt = nebVolDt(tt, 0.06, 0.035);
        vec2 rel = p.xy - VV_C;
        float R = length(rel);
        // Empty space: skip by the exact distance to the band's outer reach.
        float gap = abs(R - gR0) - (VV_REACH * VV_W + VV_WOB);
        if (gap > 0.0) {
            tt += max(gap, dt);
            continue;
        }
        float v = dot(rel, VV_TAN);
        float across = R - gR0 - frontShift(v, p.z);
        float band = exp(-(across * across) / (VV_W * VV_W));
        if (band > 0.004) {
            vec3 q = vvWarp(vec3(across * 1.1, v * 0.32 + drift, p.z + churn), gT);
            float n = nebFbm3(vec3(q.xy, q.z * 0.5), oct);
            // Far sheets are drawn wider: the steps there are longer.
            float w = w0 * (1.0 + 0.05 * tt);
            float sa = (n - 0.5) / w;
            float sb = (n - 0.43) / w;
            float threadA = exp(-sa * sa);
            float threadB = exp(-sb * sb);
            // Warm on the outer side of the front, cool on the inner.
            float side = smoothstep(-VV_W, VV_W, across);
            vec3 emit = (warm * threadA * (0.35 + 0.65 * side) + cool * threadB * (1.0 - 0.65 * side)) * 1.2 * gain;
            emit += haze * 0.012 * kBuild * (0.5 + n);
            emit *= band * smoothstep(0.3, 1.4, tt) * exp(-tt * 0.05);
            nebVolEmit(acc, emit, 0.06 * band, dt);
            if (acc.a < NEB_TRANS_MIN) break;
        }
        tt += dt;
    }

    // Beyond: a faint glow toward the remnant's centre, and stars, dimmed
    // behind bright lace.
    vec3 toC = normalize(vec3(VV_C - ro.xy, 0.0));
    vec3 bg = cool * (0.012 + 0.03 * kBuild * pow(max(dot(rd, toC), 0.0), 3.0));
    float lace = clamp(dot(acc.rgb, vec3(0.3, 0.5, 0.2)), 0.0, 1.0);
    bg += nebStars(nebSky(rd), 8.0, 2, vec2(0.0), 1.0, 0.2, hats, gT) * (1.0 - 0.4 * lace);

    vec3 col = acc.rgb + acc.a * bg;
    col *= 0.85 + 0.15 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
