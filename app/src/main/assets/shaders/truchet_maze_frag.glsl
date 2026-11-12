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
//#include lib_truchet

// Truchet Maze: Truchet's diagonal split, the one-line "10 PRINT" labyrinth,
// lifted into three dimensions and flown through. The flat style gave every
// square cell one wall on one of its two diagonals; here every cube of a
// lattice holds one glowing rod on one of its four body diagonals, corner to
// corner. Rods meet their neighbours at the lattice vertices exactly as the
// flat walls met corner to corner, so the space fills with branching chains
// of light - a circuit board grown into a cave of wires - and the eye flies
// down a corridor left open through it. It still reads as a circuit, only now
// with depth: near rods are thick tubes of light with a hot core, far ones
// sink into a dark haze, and the air between them glows where they crowd.
//
// ---- the scene and the camera -----------------------------------------------
//
// The maze lives in "maze units", one per cell. A world point w maps to the
// maze point q = eyeM + (w - eye) * s: a uniform scale about the eye (s sets
// how many cells per world unit: Complexity, k5 SPACE and the bass breathing
// move it), then a slow sideways bend of x and y as a function of z (Chaos).
// The cells round the maze's z axis are left empty, so there is a corridor
// two cells wide to fly down, and a small sphere round the eye is cut out of
// everything, for the rig's orbit and hover paths that leave the corridor.
//
// The camera is the shared rig (camFly) down +z. The cell hash wraps every 32
// cells along z, which is 64 world units at the base cell size of 2; 64
// divides the rig's 8192 travel wrap, and the eye is rebased by mod 64. The
// rebase shifts eyeM by exactly 32 cells and leaves w - eye alone, so q
// moves by a whole hash period whatever the scale is - the gyroid cavern's
// trick - and the wrap never shows. Everything else that reads maze z (the
// bend, the shimmer) uses whole multiples of 2 pi / 32 per cell, and the
// pulse reads only q - eyeM, so they survive the rebase too.
//
// ---- audio ------------------------------------------------------------------
//
// The flat style's three reactions, kept and given depth:
//   beat     on every beat a pulse of light runs out through the maze from
//            the eye, an octahedral front (|dx| + |dy| + |dz| = r, the
//            maze's own diagonal geometry, as the flat style's diamond was),
//            set by the beat-clock phase (sndPhase(SND_HIT_TIME, 1.0)). It
//            lights the rods and the glow it passes and fades as it
//            travels, so it is a moving shell, never a full-frame flash; it
//            fades away in silence (sndShaped(SND_LEVEL)).
//   onsets   swing a scattered third of the rods onto another body diagonal
//            (the rod's direction glides corner to corner over the onset
//            envelope), so corridors open and close on the hits. The first
//            frame of a swing is still the old rod: nothing snaps.
//   bass     breathes the scale (truchetBreath): the cells grow toward the
//            eye on a low end.
// The hue shifts with distance from the eye (the flat style's distance from
// the centre) and with the key. Brightness follows SND_LEVEL only.
//
// ---- macros and knobs --------------------------------------------------------
//
// Speed: uTime drives the hue drift and the shimmer. Complexity: uMorph packs
// the maze tighter (more cells per world unit). Chaos: uWarp / uTurbulence
// bend the corridor from side to side along z (the bend is measured from the
// eye's own z, so the corridor always starts round the eye); uTwist in
// view(). Energy: uIntensity scales the pulse and the bass breathing. Colour:
// rods, glow, haze and pulse are pal(). Space: the camera rig.
// k0 PULSE weights the beat pulse; k1 TEXTURE the shimmer along the rods;
// k2 FLOW how far the glow spreads round them; k3 TONE turns the hue; k4
// BUILD thickens the rods; k5 SPACE thins (high) or packs (low) the lattice.
//
// ---- why the distance estimate is safe --------------------------------------
//
// A rod lies inside its own cell's cube (its half-vector has every component
// at most 0.5, also mid-swing, since the swing mixes two such vectors), plus
// its radius R. A maze point q lies within half a cell, per axis, of its
// nearest lattice vertex v; the 8 cells sharing v cover every point within
// half a cell of q, so a rod in any other cell is at least 0.5 - R away.
// mazeDist() measures the 8 capsules exactly and clamps by 0.5 - TM_RMAX, so
// it is a true lower bound in maze units. The map w -> q stretches lengths by
// at most s * L, where L = 1 + |bend'| bounds the bend's shear, so dividing
// by s * L gives a lower bound in world units. The eye sphere is a max() with
// an exact sphere distance, which keeps the bound.
//
// ---- cost -------------------------------------------------------------------
//
// Per march step: 8 cells, each one integer hash plus two (re-roll epochs)
// and one capsule; bounded by TM_MAX_STEPS and broken on the Detail budget
// uSteps. The glow integrates along the same steps, so it costs nothing more.

#define TM_MAX_STEPS 96
#define TM_FAR 34.0
/** World units per cell at scale 1. */
#define TM_CELL 2.0
/** Cells along z before the hash repeats: 32 * 2 = 64 world units, which divides 8192. */
#define TM_ZCELLS 32.0
#define TM_ZWRAP 64.0
/** One turn per hash period along z, per cell. */
#define TM_ZW 0.19634954
/** The largest rod radius, maze units (the bound's clearance, see the header). */
#define TM_RMAX 0.085
/** World radius of the sphere cut round the eye. */
#define TM_BUBBLE 0.7
/** How far the beat front runs, maze units of |dx| + |dy| + |dz|. */
#define TM_FRONT 16.0

/** The eye, z rebased into 0..64 (see main), and its maze point. */
vec3 gEye;
vec3 gEyeM;
/** Cells per world unit, and the bend's shear bound. */
float gS;
float gLip;
/** The bend's amplitudes and the bend at the eye's own z. */
float gBendA;
float gBendB;
vec2 gBend0;
/** Rod radius, maze units. */
float gR;
/** The onset count and envelope. */
uint gHits;
float gEnv;

/** The Chaos bend of x and y as a function of maze z: whole turns per 32 cells, so it wraps with the hash. */
vec2 mazeBend(float z) {
    float t = uTime;
    return gBendA * vec2(sin(z * TM_ZW + t * 0.21), sin(z * TM_ZW * 2.0 - t * 0.17 + 1.3))
         + gBendB * vec2(sin(z * TM_ZW * 4.0 - t * 0.37 + 2.1), sin(z * TM_ZW * 4.0 + t * 0.33));
}

/** The maze point of world point w. */
vec3 mazePoint(vec3 w) {
    vec3 q = gEyeM + (w - gEye) * gS;
    q.xy += mazeBend(q.z) - gBend0;
    return q;
}

/** One body diagonal of the unit cube, as a direction with x = 1: 0..3 pick the signs of y and z. */
vec3 mazeDiag(uint i) {
    return vec3(1.0, (i & 2u) != 0u ? -1.0 : 1.0, (i & 1u) != 0u ? -1.0 : 1.0);
}

/**
 * The rod in cell `cell` as a half-vector from the cell centre (zero length:
 * an empty cell). Each cell re-rolls on one onset in 2..4, its period and
 * offset hashed from the cell (on average one in three, as the flat style),
 * and swings from the old diagonal to the new one over the onset envelope.
 */
vec3 mazeRod(vec3 cell) {
    // The corridor: the four cells round the maze z axis stay empty.
    vec2 cc = cell.xy + 0.5;
    if (dot(cc, cc) < 1.0) return vec3(0.0);
    ivec3 ic = ivec3(int(cell.x), int(cell.y), int(mod(cell.z, TM_ZCELLS)));
    uvec3 u = uvec3(ic);
    uint h0 = truchetMix(u.x * 0x9e3779b9u ^ truchetMix(u.y + 0x632be5abu ^ truchetMix(u.z * 0x85ebca6bu + 0x27d4eb2fu)));
    uint per = 2u + (h0 >> 3) % 3u;
    uint off = (h0 >> 9) % per;
    uint now = (gHits + off) / per;
    uint before = gHits + off >= 1u ? (gHits + off - 1u) / per : now;
    uint dNew = truchetMix(h0 ^ (now * 0x2c1b3c6du + 0x297a2d39u)) & 3u;
    if (now == before) return 0.5 * mazeDiag(dNew);
    uint dOld = truchetMix(h0 ^ (before * 0x2c1b3c6du + 0x297a2d39u)) & 3u;
    float k = smoothstep(0.0, 0.8, 1.0 - gEnv);
    return 0.5 * mix(mazeDiag(dOld), mazeDiag(dNew), k);
}

/** Lower bound of the distance from maze point q to the nearest rod, maze units. */
float mazeDist(vec3 q) {
    vec3 v = floor(q + 0.5);
    float d = 0.5 - TM_RMAX;
    for (int k = 0; k < 8; k++) {
        vec3 cell = v - 1.0 + vec3(float(k & 1), float((k >> 1) & 1), float((k >> 2) & 1));
        vec3 h = mazeRod(cell);
        float hh = dot(h, h);
        if (hh < 0.01) continue;
        vec3 pa = q - (cell + 0.5);
        float a = clamp(dot(pa, h) / hh, -1.0, 1.0);
        d = min(d, length(pa - h * a) - gR);
    }
    return d;
}

/** World-unit distance at w, and its maze-unit distance and maze point through the outs. */
float map(vec3 w, out float dq, out vec3 q) {
    q = mazePoint(w);
    dq = mazeDist(q);
    float d = dq / (gS * gLip);
    return max(d, TM_BUBBLE - length(w - gEye));
}

float mapOnly(vec3 w) {
    float dq;
    vec3 q;
    return map(w, dq, q);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * mapOnly(p + k.xyy * eps) + k.yyx * mapOnly(p + k.yyx * eps)
                   + k.yxy * mapOnly(p + k.yxy * eps) + k.xxx * mapOnly(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float morph = truchetMorph();

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    // Cells per world unit: Complexity packs, SPACE thins, the bass breathes.
    gS = (1.0 + 0.5 * morph) * (1.0 - 0.4 * kSpace) * truchetBreath(SND_BASS_PRESENCE) / TM_CELL;
    gBendA = 0.8 * clamp(uWarp, 0.0, 2.0);
    gBendB = 0.3 * clamp(uTurbulence, 0.0, 2.0);
    gLip = 1.0 + gBendA * TM_ZW * 3.0 + gBendB * TM_ZW * 8.0;
    gR = 0.06 * (1.0 + 0.8 * kBuild);
    gHits = uint(max(floor(SND_ONSET_COUNT), 0.0));
    gEnv = clamp(SND_ONSET_ENV, 0.0, 1.0);

    vec3 ro;
    vec3 rd;
    // The mids turn the look aside (lib_truchet's truchetTurn).
    camFly(uv - vec2(truchetTurn(), 0.0), ro, rd);
    // Exact and invisible: see the header's wrap note.
    ro.z = mod(ro.z, TM_ZWRAP);
    gEye = ro;
    gEyeM = ro / TM_CELL;
    gBend0 = mazeBend(gEyeM.z);

    // The beat pulse: one octahedral front per beat, running out from the eye.
    float ph = sndPhase(SND_HIT_TIME, 1.0);
    float front = ph * TM_FRONT;
    float amp = smoothstep(0.0, 0.08, ph) * (1.0 - ph) * clamp(sndShaped(SND_LEVEL) * 2.5, 0.0, 1.0) * e
              * (1.0 + 0.8 * kPulse);

    // March, integrating the glow of the air round the rods on the way:
    // sum of density * step length, so the glow does not depend on Detail.
    float glowK = 16.0 * (1.0 - 0.6 * kFlow);
    float fogK = 0.07;
    float tr = 0.02;
    float hitT = -1.0;
    float used = 0.0;
    vec3 hitQ = vec3(0.0);
    float glowW = 0.0;
    float glowP = 0.0;
    float glowT = 0.0;
    for (int i = 0; i < TM_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tr > TM_FAR) break;
        used = float(i);
        float dq;
        vec3 q;
        float d = map(ro + rd * tr, dq, q);
        float eps = 0.0008 * tr + 0.0006;
        if (d < eps) {
            hitT = tr;
            hitQ = q;
            break;
        }
        float stepLen = max(d * 0.9, eps);
        float dens = exp(-max(dq, 0.0) * glowK) * exp(-tr * fogK) * min(stepLen, 0.6);
        vec3 rel = abs(q - gEyeM);
        float fd = (rel.x + rel.y + rel.z - front) * 0.9;
        float ring = exp(-fd * fd);
        glowW += dens;
        glowP += dens * ring;
        glowT += dens * tr;
        tr += stepLen;
    }

    float hue0 = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.25 * kTone;
    // The flat style's hue ran with distance from the centre; here, from the eye.
    // (|dx| + |dy| + |dz| runs about 1.5 times the straight distance.)
    float glowHue = hue0 + 0.18 * 1.5 * (glowT / max(glowW, 1e-4)) * gS / 6.0;
    vec3 fogCol = pal(hue0 + 0.8) * 0.03 + pal(hue0 + 0.55) * 0.05 * pow(max(rd.z, 0.0), 6.0);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
        vec3 rel = abs(hitQ - gEyeM);
        float l1 = rel.x + rel.y + rel.z;
        float hue = hue0 + 0.18 * l1 / 6.0;
        vec3 wallC = pal(hue + 0.55);
        vec3 pulseC = pal(hue + 0.12);
        float fd = (l1 - front) * 0.9;
        float ring = exp(-fd * fd) * amp;
        // A faint shimmer runs along the rods; its z rate is 9 turns per hash
        // period (9 * TM_ZW), so the rebase lands on a whole ripple.
        float shim = 0.15 * (1.0 + 0.8 * kTexture);
        float shimmer = (1.0 - shim) + shim * sin((hitQ.x - hitQ.y) * 2.1 + t * 0.7)
                      * sin((hitQ.x + hitQ.y) * 1.7 + hitQ.z * TM_ZW * 9.0 - t * 0.5);
        // A tube of light: a hot core face-on, the colour at the rim.
        vec3 rod = wallC * (0.35 + 0.55 * ndv) * shimmer
                 + vec3(1.0) * 0.22 * pow(ndv, 4.0)
                 + wallC * fres * 0.4
                 + pulseC * ring * (1.1 + 0.6 * ndv);
        rod *= 0.55 + 0.45 * ao;
        col = mix(rod, fogCol, 1.0 - exp(-hitT * fogK));
    }

    // The glow of the air between the rods, and the pulse's shell through it.
    col += pal(glowHue + 0.55) * glowW * 0.4;
    col += pal(glowHue + 0.12) * glowP * amp * 1.6;

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
