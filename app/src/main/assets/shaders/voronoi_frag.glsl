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
// Mids (SND_MIDDLE)        turn the eye about the vertical while they climb,
//                          easing back as they fall, as lib_flame's gFlameTurn
//                          turns its structure (0.6 x sndShaped x SND_DRIVE,
//                          0.9 rad at most, 0 in silence); the causeway holds
//                          still. Mid gain scales it.
//
// ---- controls ---------------------------------------------------------------
//
// Morph domes the end faces (flat prisms to glowing pebbles). Turbulence
// roughens the column heights. Warp lets the seeds wander further, so the
// cells breathe and reshape as the flat ones did. Intensity and the rest of
// the grade are lib_scene_grade's. Knobs (each neutral at 0.5): PULSE how far
// a band lifts its column, TEXTURE the crack width, FLOW the seeds' wander,
// TONE the hue, BUILD the walls' rise, SPACE the width of the open valley.
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
#define VC_FLOOR -4.0
#define VC_CEIL 4.0
/** Horizontal run from the open valley up to the full wall height. */
#define VC_BANK 3.4
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
vec3 gEye;

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
    return smoothstep(gWidth, gWidth + VC_BANK, ax);
}

/** A column's length above the slab before the band and the dome: rough base plus the wall. */
float columnLen(float h, float h2, float wall) {
    return 0.35 + 0.6 * h * gRough + gRise * wall * (0.55 + 0.45 * h2);
}

float map(vec3 p) {
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
    float top = VC_FLOOR + columnLen(h3, h2, wall) + gLift * e;
    float bot = VC_CEIL - columnLen(h4, h3, wall) - 0.6 * gLift * e;
    float side = gGap - b;
    float colF = max(side, (p.y - top - dome) / slope);
    float colC = max(side, (bot - dome - p.y) / slope);

    // The tallest (lowest) a neighbour could reach: a full rough base, a full
    // band, a full dome, one wall-step further out.
    float reach = 0.35 + 0.6 * gRough + gRise * wallAt(ax + 2.0 * VC_CELL) + gLift * VC_EQ_MAX + gDome * VC_DOME_B;
    float hiN = VC_FLOOR + reach;
    float loN = VC_CEIL - reach;
    float dN = max(b + gGap, min(p.y - hiN, loN - p.y));

    float dSlab = min(p.y - VC_FLOOR, VC_CEIL - p.y);
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

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    gAmp = min((0.26 + 0.1 * clamp(uWarp, 0.0, 1.0)) * (1.0 + 0.4 * kFlow), 0.38);
    gLift = 1.4 * (1.0 + 0.8 * kPulse);
    gGap = 0.07 * (1.0 + 0.8 * kTexture);
    gRise = 3.0 * (1.0 + 0.6 * kBuild);
    gWidth = 4.4 * (1.0 + 0.4 * kSpace);
    gDome = 0.6 * clamp(uMorph, 0.0, 1.0);
    gRough = 1.0 + clamp(uTurbulence, 0.0, 2.0);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The mids turn the eye about the vertical (see the header).
    rd.xz = rot2(0.6 * sndShaped(SND_MIDDLE) * clamp(SND_DRIVE, 0.0, 1.0)) * rd.xz;
    // The cells repeat every 1024 along z (512 cells of 2), so this rebase is
    // exact and invisible, and keeps z precise for the cells and the normals.
    ro.z = mod(ro.z, VC_ZWRAP);
    gEye = ro;

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
            float core = exp(-gMd * 10.0) * gE * (1.0 + 1.5 * hat);
            vec3 lit = cell * (0.1 + gE * 1.6) * smoothstep(0.0, 0.35, gMd) + cell * core;
            // The rim where the face meets the crack.
            float rim = exp(-max(gB - gGap, 0.0) * 18.0) * 0.35 * gE;
            // The shaft: dark basalt glass, lit softly, glowing up toward its end.
            float endGlow = exp(-abs(p.y - gTop) * 1.4);
            vec3 shaft = cell * (0.04 + 0.12 * dif) * ao + cell * endGlow * gE * 0.25;
            col = mix(shaft, lit * (0.55 + 0.45 * ao), face) + cell * rim * face
                + pal(hue + 0.1) * fres * 0.12 + vec3(0.6) * spec * 0.25 * ao;
            // Kick light leaking up the shafts out of the cracks.
            float fromSlab = gMat < 0.5 ? p.y - VC_FLOOR : VC_CEIL - p.y;
            col += pal(hueBase + 0.6) * kick * 0.35 * exp(-fromSlab * 1.2) * (1.0 - face);
        }
        col = mix(col, fogCol, 1.0 - exp(-hitT * 0.06));
    }

    col *= 0.78 + 0.30 * swell;
    col *= 0.7 + 0.3 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
