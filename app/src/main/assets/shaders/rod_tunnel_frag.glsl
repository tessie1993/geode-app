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

// Rod Tunnel: a spiral tunnel of bead-chain rods, flown down toward the
// chrysanthemum at the end of it.
//
// The wall is a cylinder that repeats in angle (strands) and depth (cells),
// twists with depth, and holds one rod per cell. What this version adds, in
// the order the eye meets it:
//
//   - THE TUNNEL BENDS. It is warped onto lib_dmt's flight path, so it curves
//     up, down, left and right ahead of the camera, which rides the same path
//     looking along it. A spike leans the path toward the new heading
//     (uMoveDir), so the turns are steered by the music and glide in.
//   - THE RODS ARE ALIVE. Each cell has its own life: rods bud out of the
//     wall, hold, and dissolve, on a clock offset by the cell's hash, so at
//     any moment some strands are thick, some are budding and some have
//     gaps. Nothing appears in one frame - dmtLife eases both ends.
//   - THE RODS MORPH. The bead chain is a capsule whose radius is modulated
//     by a sine; uFormPhase walks that from a smooth rod through tight beads
//     to a string of near-separate pearls and back, gliding, never stepping.
//   - THE MATERIAL is lib_dmt's: banded by the cell, dispersed rim, thin-film
//     sheen, a reflected softbox. The old two-light diffuse block is gone.
//   - THE END of the tunnel is the mandala rather than a spot: the miss
//     branch draws dmtChrysanthemum on the camera-relative direction, so the
//     filigree sits on the axis you are flying toward and turns as you bank.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Three non-rigid stages, each with a stated bound, all divided out at the
// step: the tunnel warp (DMT_TUNNEL_LIP), the angular repeat with the twist
// (the old 0.6 step scale, kept), and the bead modulation (a displacement of
// amplitude ROD_BEAD_AMP at frequency ROD_BEAD_FREQ, whose gradient is bounded
// by their product). The life scale is an exact radius change and costs
// nothing.

#define ROD_TAU 6.2831853
/** Compile-time ceiling on the march: MarchBudget.MAX_STEPS. uSteps is the runtime budget. */
#define ROD_MAX_STEPS 128
#define ROD_CELL 1.0
#define ROD_RADIUS 1.6
#define ROD_FOCAL 1.3
#define ROD_FAR 26.0
#define ROD_STRANDS 14.0
#define ROD_FOG 0.09

/**
 * Bead modulation: amplitude and frequency at the deepest point of the morph.
 * The gradient of a * sin(f z) is bounded by a * f, which is what ROD_BEAD_LIP
 * divides the estimate by. At the smooth end of the morph both go to nothing.
 */
const float ROD_BEAD_AMP = 0.045;
const float ROD_BEAD_FREQ = 40.0;
const float ROD_BEAD_LIP = 1.0 + ROD_BEAD_AMP * ROD_BEAD_FREQ;

/** How long a rod lives, in seconds, and how far along a cell the seed spreads the clocks. */
const float ROD_LIFE_PERIOD = 11.0;

/** The step fraction the angular repeat and twist already needed; now also carries the two bounds above. */
const float ROD_STEP = 0.6 / (DMT_TUNNEL_LIP * ROD_BEAD_LIP);

float gTwist;
float gCellA;
float gCellZ;
float gBeadDepth;
float gLife;

float sdCapsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3 pa = p - a;
    vec3 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

float map(vec3 p) {
    // Straighten the bent tube first: everything below is in the frame where
    // the tunnel is a cylinder down +z.
    p = dmtTunnelWarp(p);
    float rad = length(p.xy);
    float ang = atan(p.y, p.x) + p.z * gTwist;
    float sector = ROD_TAU / ROD_STRANDS;
    gCellA = floor(ang / sector);
    ang = mod(ang, sector) - 0.5 * sector;
    gCellZ = floor(p.z / ROD_CELL);
    float z = mod(p.z, ROD_CELL) - 0.5 * ROD_CELL;
    vec3 q = vec3(ROD_RADIUS - rad, ang * ROD_RADIUS, z);
    // The cell's own life. The seed is the cell's hash, so each rod's clock
    // is unrelated to its neighbours'; the radius follows the envelope and a
    // dead cell returns the empty capsule's distance, which is a valid lower
    // bound for a cell that holds nothing.
    gLife = dmtLife(hash11(gCellA * 7.13 + gCellZ * 3.71), ROD_LIFE_PERIOD);
    float beads = gBeadDepth * ROD_BEAD_AMP * sin(q.z * ROD_BEAD_FREQ);
    float r = (0.14 + beads) * gLife;
    return sdCapsule(q, vec3(0.0, 0.0, -0.4), vec3(0.0, 0.0, 0.4), r);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();
    float bassA = min(uBassSmooth, 1.3);
    float midA = min(uMidSmooth, 1.3);
    float trebA = min(uTrebleSmooth, 1.3);
    float enA = min(uEnergySmooth, 1.3);
    float hit = uSpike;

    gTwist = 0.25 + 0.55 * midA;
    // Rod to pearls and back: a triangle of uFormPhase, so the morph is deep
    // in the middle of the ring and smooth at both ends, and glides because
    // uFormPhase does.
    gBeadDepth = 1.0 - abs(2.0 * fract(uFormPhase) - 1.0);

    // The camera's distance down the tunnel, INTEGRATED rather than `uTime * rate`.
    // Multiplying a running clock by a loudness-dependent rate does not speed the
    // camera up, it teleports it: at t=60s a rate moving 1.4 -> 3.6 jumps the
    // viewpoint 132 units down the tube in one frame. uFlowPhase only ever advances.
    float fly = uFlowPhase * 9.0 + uTime * 1.4;
    // A slow roll, so the horizon of the tube turns as it banks.
    float roll = uTime * 0.08 + 0.25 * sin(uTime * 0.031);
    vec3 ro;
    vec3 rd;
    dmtFlightRay(uv, ROD_FOCAL, fly, roll, ro, rd);
    // Sit a little off the axis, in the tube's own frame, so the wall is not
    // seen dead centre.
    ro.xy += vec2(0.25 * sin(uTime * 0.31), 0.25 * cos(uTime * 0.23));

    float t = 0.02;
    float hitT = -1.0;
    float near = 1e9;
    for (int i = 0; i < ROD_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > ROD_FAR) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        float eps = 0.0008 * t + 0.0004;
        if (d < eps) {
            hitT = t;
            break;
        }
        near = min(near, d);
        t += max(d * ROD_STEP, eps);
    }

    float hueShift = 0.12 * hit * clamp(uBeatResponse, 0.0, 2.0);
    vec3 coreCol = pal(0.08 + hueShift);
    // The sky: the mandala on the flight direction. The camera basis from
    // dmtFlightRay has +z along the path, so the direction is taken relative
    // to that frame rather than to the world's: the mandala is always ahead.
    vec3 rdLocal = transpose(dmtFlightBasis(fly, roll)) * rd;
    vec3 sky = dmtChrysanthemum(rdLocal, 0.62 + hueShift, enA);
    float onAxis = pow(max(rdLocal.z, 0.0), 28.0);
    float tGlow = hitT > 0.0 ? hitT : ROD_FAR;

    vec3 col;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        map(p);
        float cellA = gCellA;
        float cellZ = gCellZ;
        float life = gLife;
        float e = max(0.0012 * hitT, 0.0005);
        vec3 n = normalAt(p, e);
        float band = hash11(cellA * 7.13 + cellZ * 3.71);
        float hue = band * 0.5 + hueShift + 0.015 * cellZ;
        // A rod is thinnest where it has just budded or is dissolving, and the
        // pearls are thinner than the rod: both read as translucency.
        float thin = (1.0 - life) * 0.7 + 0.4 * gBeadDepth;
        col = dmtShade(n, rd, hue, band * 2.0 + cellZ * 0.15, 0.85 + 0.15 * life, thin, trebA);
        // Young rods glow as they arrive, dying ones as they leave.
        col += pal(hue + 0.4) * (1.0 - life) * 0.35;
        col = mix(col, sky, 1.0 - exp(-hitT * ROD_FOG));
    } else {
        col = sky;
        // The halo off the nearest rod the ray slid past.
        col += dmtHalo(near, 0.55 + hueShift, 4.0, 0.22 + 0.3 * enA);
    }
    col += coreCol * (0.6 + 0.9 * bassA) * onAxis * 2.0 / (1.0 + 0.02 * tGlow * tGlow);
    col += coreCol * 0.06 * (0.6 + 0.9 * bassA) / (1.0 + dot(uv, uv) * 3.0);
    // Sparks drifting between the rods, fading in with each new spawn.
    col += mix(coreCol, vec3(1.0), 0.4) * fluidMotes(uv, 6.0, 0.13) * 0.18;

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.7 + 0.3 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
