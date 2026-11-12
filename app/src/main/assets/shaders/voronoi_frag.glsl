#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Turbulence and Warp are spent on the columns (see "controls"), not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_shell

// Voronoi: the flat cell field, built as a causeway. The plane's Voronoi
// cells are extruded into basalt columns: a floor of them below, a ceiling of
// them hanging above, and both rising into walls on either side of the
// flight. Each column is one cell and still lights up with its own frequency
// band, exactly as the flat style did: its end face glows pal(cellId) by
// sndEq(cellId), dark at the seed's ring and bright at the seed itself. Thin
// cracks run between the columns down to a base slab that glows from below.
//
// The camera is the shared rig (camFly). The cells are 2 world units wide and
// hash with a period of 512 cells along z, 1024 world units, which divides the
// rig's 8192 wrap, so the eye is rebased by 1024 and the wrap is invisible.
// The hue drift along z has period 64, which divides 1024 too.
//
// ---- audio ------------------------------------------------------------------
//
// sndEq(cellId)            per column: its height (floor columns rise, ceiling
//                          columns drop) and the glow of its end face, the flat
//                          style's mapping with depth added.
// Kick (SND_KICK_ENV)      the cracks between the columns flare from the slab.
// Hat (SND_HAT_ENV)        the seed cores on the end faces sparkle.
// Pitch (SND_HELD_NOTE_CHANGE) slides the palette.
// SND_ENERGY_TREND         overall level.
//
// ---- controls ---------------------------------------------------------------
//
// Morph domes the end faces (flat prisms to glowing pebbles). Turbulence
// roughens the column heights. Warp lets the seeds wander further, so the
// cells breathe and reshape as the flat ones did. Intensity and the rest of
// the grade are lib_scene_grade's. Knobs (each neutral at 0.5): PULSE how far
// a band lifts its column, TEXTURE the crack width, FLOW the seeds' wander,
// TONE which way the columns reach (a high melody lets the ceiling's columns
// hang long and the floor's shrink, a low one grows the floor's up toward the
// ceiling; and a small hue offset), BUILD the walls' rise, SPACE the width of
// the open valley. The neighbour bound takes the longer side's factor, so it
// holds either way.
//
// ---- shape inputs (lib_shell) ----------------------------------------------
//
// Every tunable of the causeway is a normalized input, mapped to its range in
// one place (the top of main) and read from globals. 0.5 is the look as tuned:
// the knobs rest there exactly while Note & key is off, and a param at its
// default 0 maps to its rest through shellMapS. Music reaches these only
// through the knobs and params (the reaction matrix); none reads the sound.
//
//   input       driver             lo .. rest .. hi
//   gLift       k0 PULSE           how far a full band lifts 0.84 .. 1.4 .. 1.96
//   gGap        k1 TEXTURE         crack half-width 0.042 .. 0.07 .. 0.098
//   sCore       k1 TEXTURE         seed core sharpness 16 .. 10 .. 6
//   gAmp        Warp x k2 FLOW     seed wander 0.26 .. 0.36, times 0.8 .. 1 .. 1.2 (<= 0.38)
//   gLean       k3 TONE            ceiling against floor -0.6 .. 0 .. 0.6
//   gRise       k4 BUILD           the walls' rise 2.1 .. 3.0 .. 3.9
//   sBank       k4 BUILD           the run up to full wall 4.5 .. 3.4 .. 2.4
//   sBaseLen    k4 BUILD           a column's base length 0.15 .. 0.35 .. 0.7
//   gWidth      k5 SPACE           the open valley 3.52 .. 4.4 .. 5.28
//   sSlab       k5 SPACE           floor and ceiling from the axis 3.4 .. 4.0 .. 5.0
//   sFog        k5 SPACE           fog density 0.09 .. 0.06 .. 0.035
//   gDome       Morph              end-face dome 0 .. 0.6
//   gRough      Turbulence         height roughness 1 .. 2.5 (at Turbulence 1.5)
//
// The cell size (2) stays a constant: 512 cells of it are the 1024 the z hash
// and the eye's rebase wrap on. The columns go through its shell wrapper, so the
// shell's spawn, fractal, front and back hooks reach them.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The cell boundary distance b is the exact distance to the nearest bisector
// of the nearest seed (the seeds stay inside 0.12..0.88 of their cell, so a
// 3 x 3 search finds both the seed and its neighbours), and it is 1-Lipschitz.
// A column is max(gap - b, cap), the cap divided by its dome's slope bound.
// Only the point's own column is measured; every other column is at least
// b + gap away sideways, so that bound applies unless the point is below the
// tallest neighbour floor top or above the lowest neighbour ceiling bottom
// (hiN, loN, taken one wall-step further out). A sphere around the eye is
// carved out of everything, so the eye is never inside a column.

#define VC_MAX_STEPS 96
#define VC_FAR 44.0
#define VC_TAU 6.2831853
/** World units per cell. */
#define VC_CELL 2.0
/** Cells along z before the hash repeats: 512 * 2 = 1024 world units, which divides 8192. */
#define VC_ZCELLS 512.0
#define VC_ZWRAP 1024.0
/** The largest band value a column answers, so the neighbour bound holds. */
#define VC_EQ_MAX 1.2
#define VC_BUBBLE 0.9
/** The dome reads b only up to here, so its height is bounded. */
#define VC_DOME_B 0.8

// Per frame (main).
float gAmp;
float gLift;
float gGap;
float gRise;
float gWidth;
float gDome;
float gRough;
/** TONE: the ceiling columns' share against the floor's (0 at rest). */
float gLean;
vec3 gEye;
/** Shape inputs (see the header): the wall's run, a column's base, the slabs, fog, the seed core. */
float sBank;
float sBaseLen;
float sSlab;
float sFog;
float sCore;

// Per map() call: the nearest cell.
float gB;
float gMd;
float gH;
float gE;
float gMat;
float gTop;

/** The flat style's band read, at an explicit LOD: map() runs inside the march loop. */
float vcEq(float x) {
    return textureLod(uSoundTex, vec2(clamp(x, 0.0, 1.0), 2.5 / 4.0), 0.0).r * uSoundDrive;
}

uint vcHash(vec2 cell) {
    return uhash3(uvec3(ivec3(int(cell.x), 0x5a, int(mod(cell.y, VC_ZCELLS)))));
}

/** The seed's place inside its cell, 0.12..0.88 on each axis, wandering slowly. */
vec2 vcSeed(vec2 cell) {
    uint u = vcHash(cell);
    float h = uhashUnit(u);
    float h2 = uhashUnit(uhash(u));
    float h3 = uhashUnit(uhash(u ^ 0x27d4u));
    return 0.5 + gAmp * sin(uTime * (0.3 + 0.25 * h2) + VC_TAU * vec2(h, h3));
}

/**
 * x in cell units. Returns (b, md, cell.x, cell.y): the distance to the cell
 * boundary, the distance to the seed, and the nearest cell. `seedX` is the
 * nearest seed's x, in cell units.
 */
vec4 voronoi(vec2 x, out float seedX) {
    vec2 g = floor(x);
    vec2 f = x - g;
    float md = 8.0;
    vec2 mr = vec2(0.0);
    vec2 mg = vec2(0.0);
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 n = vec2(float(i), float(j));
            vec2 r = n + vcSeed(g + n) - f;
            float d = dot(r, r);
            if (d < md) {
                md = d;
                mr = r;
                mg = n;
            }
        }
    }
    float b = 8.0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 n = mg + vec2(float(i), float(j));
            vec2 r = n + vcSeed(g + n) - f;
            vec2 dr = r - mr;
            if (dot(dr, dr) > 1e-5) b = min(b, dot(0.5 * (mr + r), normalize(dr)));
        }
    }
    seedX = x.x + mr.x;
    return vec4(b, sqrt(md), g + mg);
}

float wallAt(float ax) {
    return smoothstep(gWidth, gWidth + sBank, ax);
}

/** A column's length above the slab before the band and the dome: rough base plus the wall. */
float columnLen(float h, float h2, float wall) {
    return sBaseLen + 0.6 * h * gRough + gRise * wall * (0.55 + 0.45 * h2);
}

float mapField(vec3 p) {
    float floorY = -sSlab;
    float ceilY = sSlab;
    float seedX;
    vec4 v = voronoi(p.xz / VC_CELL, seedX);
    float b = v.x * VC_CELL;
    uint u = vcHash(v.zw);
    float h = uhashUnit(u);
    float h2 = uhashUnit(uhash(u));
    float h3 = uhashUnit(uhash(u ^ 0x27d4u));
    float h4 = uhashUnit(uhash(u ^ 0x9e37u));

    float ax = abs(seedX * VC_CELL);
    float wall = wallAt(ax);
    float e = clamp(0.5 * vcEq(h), 0.0, VC_EQ_MAX);

    float dome = gDome * min(b, VC_DOME_B);
    float slope = sqrt(1.0 + gDome * gDome);
    float top = floorY + columnLen(h3, h2, wall) * (1.0 - gLean) + gLift * e;
    float bot = ceilY - columnLen(h4, h3, wall) * (1.0 + gLean) - 0.6 * gLift * e;
    float side = gGap - b;
    float colF = max(side, (p.y - top - dome) / slope);
    float colC = max(side, (bot - dome - p.y) / slope);

    // The tallest (lowest) a neighbour could reach: a full rough base, a full
    // band, a full dome, one wall-step further out.
    float reach = (sBaseLen + 0.6 * gRough + gRise * wallAt(ax + 2.0 * VC_CELL)) * (1.0 + abs(gLean))
                + gLift * VC_EQ_MAX + gDome * VC_DOME_B;
    float hiN = floorY + reach;
    float loN = ceilY - reach;
    float dN = max(b + gGap, min(p.y - hiN, loN - p.y));

    float dSlab = min(p.y - floorY, ceilY - p.y);
    float dCol = min(colF, colC);
    float d = min(min(dCol, dN), dSlab);
    d = max(d, VC_BUBBLE - length(p - gEye));

    gB = b;
    gMd = v.y;
    gH = h;
    gE = e;
    gMat = dSlab < dCol ? 2.0 : (colF < colC ? 0.0 : 1.0);
    gTop = gMat < 0.5 ? top : bot;
    return d;
}

/** mapField through lib_shell's hooks: exactly mapField(p) at rest, and mapField read once, in a loop the compiler cannot unroll. */
float mapFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 4; i++) {
        if (shellPass3(i)) r = shellKeep3(i, mapField(shellAt3(i)));
    }
    return r;
}

/** The causeway as the shell sees it: spawn, fractal, front and back route through here (identity at rest). */
float map(vec3 p) {
    return mapFieldGrown(p);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    // ---- shape inputs: each normalized input mapped to its range, here only (see the header).
    gLift = shellMap(shellKnob(0), 0.84, 1.4, 1.96);
    gGap = shellMap(shellKnob(1), 0.042, 0.07, 0.098);
    sCore = shellMap(shellKnob(1), 16.0, 10.0, 6.0);
    gAmp = min(shellMapS(clamp(uWarp, 0.0, 1.0), 0.16, 0.26, 0.36) * shellMap(shellKnob(2), 0.8, 1.0, 1.2), 0.38);
    gLean = shellMap(shellKnob(3), -0.6, 0.0, 0.6);
    gRise = shellMap(shellKnob(4), 2.1, 3.0, 3.9);
    sBank = shellMap(shellKnob(4), 4.5, 3.4, 2.4);
    sBaseLen = shellMap(shellKnob(4), 0.15, 0.35, 0.7);
    gWidth = shellMap(shellKnob(5), 3.52, 4.4, 5.28);
    sSlab = shellMap(shellKnob(5), 3.4, 4.0, 5.0);
    sFog = shellMap(shellKnob(5), 0.09, 0.06, 0.035);
    gDome = shellMapS(clamp(uMorph, 0.0, 1.0), -0.6, 0.0, 0.6);
    gRough = shellMapS(clamp(uTurbulence / 1.5, 0.0, 1.0), -0.5, 1.0, 2.5);

    vec2 uv = view();
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The cells repeat every 1024 along z (512 cells of 2), so this rebase is
    // exact and invisible, and keeps z precise for the cells and the normals.
    ro.z = mod(ro.z, VC_ZWRAP);
    gEye = ro;
    // The grow hooks (lib_shell) open round this eye, in the rebased frame the world is read in.
    shellSetup3(ro, 3.0, 0.5 * VC_CELL, false);

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float hueBase = 0.22 * clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0) + 0.15 * kTone;

    float t = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < VC_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        float d = map(ro + rd * t);
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.9;
        if (t > VC_FAR) break;
    }

    // The valley's far end glows faintly down the travel axis.
    float ahead = pow(max(rd.z, 0.0), 8.0);
    vec3 fogCol = pal(hueBase + 0.55) * (0.03 + 0.12 * ahead) + vec3(0.004, 0.003, 0.006);
    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        float occ = 0.0;
        float sc = 1.0;
        for (int k = 0; k < 3; k++) {
            float hk = 0.1 + 0.16 * float(k);
            occ += (hk - map(p + n * hk)) * sc;
            sc *= 0.65;
        }
        float ao = clamp(1.0 - 1.6 * occ, 0.25, 1.0);
        // Restore the hit cell's globals after the normal and AO taps.
        map(p);

        vec3 L = normalize(vec3(-0.35, 0.85, 0.4));
        float dif = 0.5 + 0.5 * dot(n, L);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 32.0);
        float hue = hueBase + gH + 0.06 * sin(p.z * (VC_TAU / 64.0));
        vec3 cell = pal(hue);

        if (gMat > 1.5) {
            // The slab at the bottom of the cracks: light from below, kicked.
            col = pal(hueBase + 0.6) * (0.05 + 0.9 * kick) * ao;
        } else {
            // The flat style's cell, drawn on the column's end face: dim at the
            // seed's ring, a bright core at the seed, both by the cell's band.
            float face = smoothstep(0.6, 0.95, abs(n.y));
            float core = exp(-gMd * sCore) * gE * (1.0 + 1.5 * hat);
            vec3 lit = cell * (0.1 + gE * 1.6) * smoothstep(0.0, 0.35, gMd) + cell * core;
            // The rim where the face meets the crack.
            float rim = exp(-max(gB - gGap, 0.0) * 18.0) * 0.35 * gE;
            // The shaft: dark basalt glass, lit softly, glowing up toward its end.
            float endGlow = exp(-abs(p.y - gTop) * 1.4);
            vec3 shaft = cell * (0.04 + 0.12 * dif) * ao + cell * endGlow * gE * 0.25;
            col = mix(shaft, lit * (0.55 + 0.45 * ao), face) + cell * rim * face
                + pal(hue + 0.1) * fres * 0.12 + vec3(0.6) * spec * 0.25 * ao;
            // Kick light leaking up the shafts out of the cracks.
            float fromSlab = gMat < 0.5 ? p.y + sSlab : sSlab - p.y;
            col += pal(hueBase + 0.6) * kick * 0.35 * exp(-fromSlab * 1.2) * (1.0 - face);
        }
        col = mix(col, fogCol, 1.0 - exp(-hitT * sFog));
    }

    col *= 0.78 + 0.30 * swell;
    col *= 0.7 + 0.3 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
