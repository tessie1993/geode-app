#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_arbor

// Arbor Roots: a flight through the earth under a forest. The old style was a
// flat cut through the soil below one tree; now the eye travels inside that
// cut. The ground is a ceiling overhead, and from it, one per 8 x 8 cell, a
// tree's root system hangs down into the dark, forking and wandering around
// stones that float in the soil. Bedrock rolls along below, and the threads of
// mycelium glow on every surface the light finds. The soil itself is the
// medium the rays cross: a thick, warm fog, layered in strata by height.
//
// The roots are arborTree (the 3D fold) grown DOWNWARD: each cell flips y so
// the trunk enters at the ceiling, turns the tree by a hashed yaw, rescales it
// and reseeds its twist, so no two neighbours are the same tree. Gravity is
// negative `bend` in the flipped frame, which pulls every fork toward world
// down: roots plunge where branches would reach for light.
//
// ---- camera ------------------------------------------------------------------
//
// camFly: the eye rides the rig's path down +z through the roots' lower
// fringe (the path wanders |y| < ~1.2; the root tips reach to y ~ -1.6). The
// eye's z is rebased by RT_ZWRAP (1024): root cells are 8 deep with their hash
// wrapped every 128 cells, stone cells 2 deep wrapped every 512, the ceiling
// and floor sines have z periods 16 and 32, and the shading noise lattice is
// wrapped in z so its world period is 1024 too. 1024 divides the rig's 8192
// travel wrap, so neither the rebase nor the wrap ever shows.
//
// ---- audio ------------------------------------------------------------------
//
// Sub (sndLevel(SND_SUB_ATT)) SPLAYS the roots, as before: it widens every
// fork angle, so a deep sub passage fans every root system out sideways
// through the soil and a quiet one draws it back into narrow taps. The beat
// (SND_KICK_ENV) sends a sap front down the roots, trunk to tips, as a change
// of HUE along the root (amber to the palette's cool end) at near-constant
// lightness, not a flash; the front also leaves a faint glow in the soil
// around the roots it passes. Swell ((0.5 * (SND_BASS_PRESENCE +
// SND_MID_PRESENCE))) brightens the mycelium threads and the overall level.
//
// ---- macros and knobs ------------------------------------------------------------
//
// Speed: uTime drives the roots' slow creep, their sway and the threads'
// drift. Complexity: uMorph adds a fold level to every root system
// (arbLevels, which also follows the Detail budget). Chaos: uTurbulence and
// uWarp (arbChaos) raise the per-tree twist spread and the sway, the roots
// kinking harder; uTwist winds every fork further round. Colour: soil, roots,
// sap and threads from pal(). Energy: uIntensity scales the sub splay and the
// sap front. Space: the camera rig.
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the sap front's
// strength, k1 TEXTURE the mycelium's thread width, k2 FLOW the roots' creep
// and sway, k3 TONE the hue, k4 BUILD the sap's glow in the soil, k5 SPACE the
// soil fog's density.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// Roots: each cell measures only its own tree, so the distance is clamped to
// the cell boundary unless the point is below every root's reach (RT_BOTTOM:
// trunk length / (1 - ratio) times the largest scale, from the base). The
// fold is a true lower bound except where a twist swings a twig across a fold
// plane (see lib_arbor), so the march steps at 0.75 of the estimate. Stones:
// one ellipsoid per 2-unit cell, offset and sized so it stays 0.3 inside its
// cell; the ellipsoid's estimate is divided by its largest axis scale, and
// the neighbour bound is the cell edge plus that 0.3. Ceiling and floor are
// planes with sine wobble whose slope is at most ~0.45, scaled by 0.88. A
// sphere around the eye is carved out of roots and stones, so the eye is
// never inside one.

#define RT_MAX_STEPS 112
#define RT_FAR 34.0
#define RT_TAU 6.2831853
/** The ground overhead: roots hang from here. */
#define RT_CEIL 3.2
/** Bedrock below the deepest roots. */
#define RT_FLOOR -3.6
/** Root system cell, world units, in x and z. */
#define RT_CELL 8.0
/** Root cells along z before their hash repeats: 128 * 8 = 1024. */
#define RT_ZCELLS 128.0
/** Rebase period of the eye's z; divides 8192. */
#define RT_ZWRAP 1024.0
/** Where the tree's base sits, above the ceiling so the taproot enters through it. */
#define RT_BASE (RT_CEIL + 0.4)
/** Lowest any root can reach: 1.25 / (1 - 0.74) * 1.05 below the base, plus its girth. */
#define RT_BOTTOM (RT_BASE - 5.2)
/** Stone cell, world units, on every axis; z hash wraps every 512 cells. */
#define RT_STONE 2.0
#define RT_BUBBLE 0.7

vec3 gEye;
float gTime;
float gGrow;
float gTilt;
float gBend;
float gTwist;
float gSway;
/** Set by map(): 0 root, 1 stone, 2 soil (ceiling or floor). */
float gMat;
/** Set by map(): the root field alone, for the sap's glow in the soil. */
float gRootD;
/** Set by map(): the stone's own hash, for its tint. */
float gStoneH;

/**
 * Value noise whose lattice wraps in z every `pz` cells, so a noise read at
 * frequency f has a world z period of pz / f. Same quintic as vnoise3.
 */
float wnoise3(vec3 p, float pz) {
    vec3 f = floor(p);
    vec3 t = p - f;
    vec3 w = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    int x0 = int(f.x);
    int y0 = int(f.y);
    int z0 = int(mod(f.z, pz));
    int z1 = int(mod(f.z + 1.0, pz));
    float c000 = hashCell(ivec3(x0, y0, z0));
    float c100 = hashCell(ivec3(x0 + 1, y0, z0));
    float c010 = hashCell(ivec3(x0, y0 + 1, z0));
    float c110 = hashCell(ivec3(x0 + 1, y0 + 1, z0));
    float c001 = hashCell(ivec3(x0, y0, z1));
    float c101 = hashCell(ivec3(x0 + 1, y0, z1));
    float c011 = hashCell(ivec3(x0, y0 + 1, z1));
    float c111 = hashCell(ivec3(x0 + 1, y0 + 1, z1));
    float x00 = mix(c000, c100, w.x);
    float x10 = mix(c010, c110, w.x);
    float x01 = mix(c001, c101, w.x);
    float x11 = mix(c011, c111, w.x);
    return mix(mix(x00, x10, w.y), mix(x01, x11, w.y), w.z);
}

/** The root system of the cell p is in, clamped to the cell (see the header). */
float rootField(vec3 p) {
    vec2 cell = floor(p.xz / RT_CELL);
    vec2 f = p.xz - (cell + 0.5) * RT_CELL;
    uint u = uhash3(uvec3(ivec3(int(cell.x), 7, int(mod(cell.y, RT_ZCELLS)))));
    float h1 = uhashUnit(u);
    u = uhash(u);
    float h2 = uhashUnit(u);
    u = uhash(u);
    float h3 = uhashUnit(u);
    vec2 base = (vec2(h1, h2) - 0.5) * 1.0;
    float sc = 0.78 + 0.27 * h3;
    // Flip y: the trunk grows down from the ceiling.
    vec3 q = vec3(f.x - base.x, RT_BASE - p.y, f.y - base.y);
    q.xz = rot2(h1 * RT_TAU) * q.xz;
    float twist = gTwist + (0.5 + 0.4 * gSway) * (h2 - 0.5) + gSway * sin(gTime * (0.4 + 0.3 * h3) + h1 * RT_TAU);
    ArborForm form = ArborForm(2.0, gTilt, gBend, twist, 0.74, 1.25, 0.085, 0.74);
    float d = arborTree(q / sc, form, gGrow) * sc;
    vec2 af = abs(f);
    float toEdge = 0.5 * RT_CELL - max(af.x, af.y);
    return min(d, max(toEdge, RT_BOTTOM - p.y));
}

/** The stones: one squashed pebble in some 2-unit cells, fewer near the flight path. */
float stoneField(vec3 p) {
    vec3 cell = floor(p / RT_STONE);
    vec3 f = p - (cell + 0.5) * RT_STONE;
    uint u = uhash3(uvec3(ivec3(int(cell.x), int(cell.y), int(mod(cell.z, 512.0)))));
    float h = uhashUnit(u);
    u = uhash(u);
    float hr = uhashUnit(u);
    u = uhash(u);
    float hx = uhashUnit(u);
    float yc = (cell.y + 0.5) * RT_STONE;
    float keep = 0.62 + 0.3 * (1.0 - smoothstep(0.8, 2.4, abs(yc)));
    float r = 0.2 + 0.25 * hr;
    // Offset at most 0.25 per axis, radius at most 0.45: 0.3 clear of the cell wall.
    vec3 q = f - (vec3(hx, hr, h) - 0.5) * 0.5;
    float ds = h > keep ? (length(q * vec3(1.0, 1.4, 1.0)) - r) / 1.4 : 1e3;
    vec3 af = abs(f);
    float toEdge = 0.5 * RT_STONE - max(max(af.x, af.y), af.z);
    gStoneH = hr;
    return min(ds, toEdge + 0.3);
}

float map(vec3 p) {
    float dr = rootField(p);
    float ds = stoneField(p);
    float ceilY = RT_CEIL + 0.25 * sin(p.x * 0.7) * sin(p.z * (RT_TAU / 16.0));
    float floorY = RT_FLOOR + 0.4 * sin(p.x * 0.45 + 1.0) * sin(p.z * (RT_TAU / 32.0)) + 0.2 * sin(p.x * 1.1 + p.z * (RT_TAU / 16.0));
    float soil = min(ceilY - p.y, p.y - floorY) * 0.88;
    float solid = max(min(dr, ds), RT_BUBBLE - length(p - gEye));
    gRootD = dr;
    gMat = soil < solid ? 2.0 : (dr < ds ? 0.0 : 1.0);
    return min(solid, soil);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** The soil between the roots, as seen down a ray: warm topsoil above, dark clay below. */
vec3 fogColour(vec3 rd, float hue, float swell) {
    vec3 top = mix(vec3(0.16, 0.10, 0.06), pal(hue) * 0.22, 0.4);
    vec3 deep = mix(vec3(0.05, 0.035, 0.025), pal(hue + 0.05) * 0.08, 0.4);
    vec3 c = mix(deep, top, smoothstep(-0.7, 0.7, rd.y)) * 0.45;
    return c + pal(hue + 0.45) * 0.012 * (1.0 + swell);
}

void main() {
    vec2 uv = arbView();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024 (see the header), so
    // this shift is invisible, and it keeps highp z precise far down the
    // 8192 travel.
    ro.z = mod(ro.z, RT_ZWRAP);
    gEye = ro;

    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float energy = uIntensity;
    float chaos = arbChaos();
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float sub = clamp(sndLevel(SND_SUB_ATT), 0.0, 1.0) * energy;
    float beat = clamp(SND_KICK_ENV * energy, 0.0, 1.0);

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float hue = 0.07 + 0.02 * sin(t * 0.03) + 0.3 * kTone;

    gTime = t;
    gGrow = arbLevels(7.0, 1.0);
    // Sub widens every fork; the splay is the old style's, now in 3D.
    gTilt = 0.42 + 0.30 * sub;
    // Negative bend in the flipped frame turns every fork toward world down.
    gBend = -0.10 - 0.03 * sin(t * 0.13);
    float flow = 1.0 + 1.5 * kFlow;
    gTwist = 2.1 + 0.6 * uTwist + 0.05 * flow * sin(t * 0.2);
    gSway = (0.04 + 0.22 * chaos) * flow;

    // Sap front: depth along a tree is level + along; the front runs from
    // the trunk (0) to the tips while the beat envelope falls.
    float front = (1.0 - beat) * (gGrow + 1.5);
    float sapOn = smoothstep(0.0, 0.15, beat) * clamp(1.0 + 1.6 * kPulse, 0.0, 2.0);

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float hitMat = 2.0;
    float hitDepth = 0.0;
    float hitStone = 0.0;
    float glow = 0.0;
    for (int i = 0; i < RT_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float depth = gArbLevel + gArbAlong;
        glow += exp(-pow((depth - front) * 1.4, 2.0)) * exp(-max(gRootD, 0.0) * 10.0) * min(d, 0.5);
        if (d < 0.0012 + 0.0008 * tm) {
            hitT = tm;
            hitMat = gMat;
            hitDepth = depth;
            hitStone = gStoneH;
            break;
        }
        tm += d * 0.75;
        if (tm > RT_FAR) break;
    }

    vec3 fog = fogColour(rd, hue, swell);
    vec3 rootCol = mix(vec3(0.62, 0.46, 0.30), pal(hue + 0.02), 0.45);
    vec3 sapCol = pal(hue + 0.5) * 0.85;
    vec3 col = fog;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0015 + 0.001 * hitT);
        // The headlight rides just above the eye.
        vec3 lp = ro + uCamBasis[1] * 0.4;
        vec3 toL = lp - p;
        float dl = length(toL);
        float atten = 1.0 / (1.0 + 0.03 * dl * dl);
        vec3 lit = arbLight(n, rd, toL / max(dl, 1e-4), 0.4, 24.0);
        float ao = arbStepAO(used, uSteps);

        // Soil strata, by height, broken by a z-wrapped noise.
        float sn = wnoise3(p * 1.5, 1536.0);
        float strata = fract(p.y * 0.9 + sn * 0.5);
        vec3 soilA = mix(vec3(0.16, 0.10, 0.06), pal(hue) * 0.22, 0.4);
        vec3 soilB = mix(vec3(0.09, 0.06, 0.04), pal(hue + 0.05) * 0.12, 0.4);
        vec3 soil = mix(soilA, soilB, smoothstep(0.3, 0.7, strata)) * (0.75 + 0.35 * sn) * 2.2;

        vec3 base;
        float threadAmt;
        float sap = 0.0;
        if (hitMat < 0.5) {
            sap = exp(-pow((hitDepth - front) * 1.4, 2.0)) * sapOn;
            base = mix(rootCol, sapCol, clamp(sap, 0.0, 1.0));
            threadAmt = 0.4;
        } else if (hitMat < 1.5) {
            base = mix(vec3(0.32, 0.30, 0.28), pal(hue + 0.5) * 0.3, 0.3) * (0.7 + 0.3 * hitStone) * 1.6;
            threadAmt = 0.7;
        } else {
            base = soil;
            threadAmt = 1.0;
        }
        col = base * (0.05 + 0.95 * lit.x * atten) * ao;
        col += base * lit.y * 0.18 * ao + vec3(lit.z) * 0.12 * atten;
        // Warm light seeping down from the ground overhead onto upward faces.
        col += base * 0.04 * clamp(n.y * 0.5 + 0.5, 0.0, 1.0) * smoothstep(-1.0, RT_CEIL, p.y);
        col += sapCol * sap * 0.12;

        // Mycelium: thin glowing threads along a noise field's zero set,
        // drifting slowly in x (no z drift, so the 1024 wrap holds).
        float m = wnoise3(p * 2.0 + vec3(t * 0.04, 0.0, 0.0), 2048.0) - 0.5;
        float thread = 1.0 - smoothstep(0.0, 0.03 * clamp(1.0 + 1.6 * kTexture, 0.2, 2.0), abs(m));
        col += pal(hue + 0.45) * thread * threadAmt * (0.14 + 0.10 * swell) * ao;

        col = mix(col, fog, 1.0 - exp(-hitT * 0.06 * clamp(1.0 + 1.4 * kSpace, 0.2, 2.0)));
    }

    // The sap's glow in the soil around the roots it passes, already
    // weighted by how far the ray went through it.
    col += sapCol * glow * sapOn * 0.2 * clamp(1.0 + 1.6 * kBuild, 0.0, 2.0);

    col *= 0.9 + 0.12 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
