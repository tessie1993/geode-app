#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the honeycomb itself, not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_shell

// Hexgrid: a hall between two honeycombs. The floor is a field of hexagonal
// columns and the ceiling is its mirror; every column is one cell of the old
// flat grid, and it still hears its own frequency band, only now the band
// raises it instead of lighting it. Its cap glows in the band's colour with
// the energy of that band, and a bright rim runs round the cap's edge, as the
// flat cells had. Near the travel axis the columns stay short; further out
// they climb until floor and ceiling meet in walls, so the hall has sides.
//
// The camera is the shared rig (camFly), flown straight down +z. The hex
// lattice is (2 / sqrt 3, 2) in world units, so its z period is 2; the cell
// hash wraps at 1024 cells along z and the Warp twist has a 64 period. All
// are powers of two dividing 8192, so the rig's travel wrap is invisible, and
// the eye is rebased into 0..1024 in main.
//
// ---- audio ------------------------------------------------------------------
//
// Each cell hashes to a spot on the spectrum (sndSpectrum(band)), scaled by
// the attack-smoothed volume as before: that sets the column's height and its
// cap glow. A column and its twin above share the band, so the hall breathes
// symmetrically. Kick (SND_KICK_ENV) punches the columns just ahead of the eye
// up a little more. Highs (sndShaped(SND_HIGH)) brighten the cap rims. A pitch
// change (SND_HELD_NOTE_CHANGE) slides the hue. Overall level rides
// SND_ENERGY_TREND.
//
// Knobs: k0 PULSE the kick lift, k1 TEXTURE the rim width, k2 FLOW the side
// glow band, k3 TONE the hall's width (a high melody draws the walls in close
// round the flight into a canyon, a low one pushes them out over a wide open
// floor; and a hue offset), k4 BUILD the cap glow, k5 SPACE the hall's
// height. Morph thins the columns and bevels their caps, Warp twists the hall
// about its axis along z, Turbulence lets every column bob on its own.
//
// ---- shape inputs (lib_shell) ----------------------------------------------
//
// Every tunable of the hall is a normalized input, mapped to its range in one
// place (the top of main) and read from globals. 0.5 is the look as tuned: the
// knobs rest there exactly while Note & key is off, and a param at its default
// 0 maps to its rest through shellMapS. Music reaches these only through the
// knobs and params (the reaction matrix); none reads the sound.
//
//   input       driver           lo .. rest .. hi
//   sBase       k4 BUILD         a column's height at rest 0.04 .. 0.12 .. 0.4
//   sWallH      k4 BUILD         the walls' extra height 1.4 .. 2.2 .. 3.0
//   sRise       k4 BUILD         the height a full band adds 0.8 .. 1.3 .. 1.8
//   gFloor      k5 SPACE         the floor's depth 2.05 .. 2.5 .. 2.95
//   gWide       k3 TONE          the hall's width 1.25 .. 1 .. 0.75
//   sFog        k5 SPACE         fog density 0.1 .. 0.06 .. 0.03
//   gRad        Morph            column radius, of the apothem 0.9 .. 0.55
//   gBev        Morph            cap bevel 0.05 .. 0.25
//   gWarp       Warp             the hall's twist 0 .. 0.35
//   gTurb       Turbulence       each column's bob 0 .. 0.525 (at Turbulence 1.5)
//
// The lattice (2 / sqrt 3 by 2), its hash wrap (1024) and the twist period
// (64) stay constants: each is a power of two the 8192 travel wrap relies on.
// The columns go through its shell wrapper, so the shell's spawn, fractal, front
// and back hooks reach them.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Each cell measures only its own column (max of the hex face planes, the cap
// plane and a 45 degree chamfer: each a unit-gradient plane, so a lower bound).
// A neighbour can be taller, so that distance is clamped to the cell's own
// boundary unless the point is above the tallest any column further out could
// reach (hiN, which takes the largest band reading, kick and bob). The Warp
// rotation grows the gradient by at most |x| times its z rate, and the step is
// scaled down by that. A sphere around the eye is carved out of everything,
// so the eye can never sit inside a column.

#define HG_MAX_STEPS 96
#define HG_FAR 40.0
#define HG_TAU 6.2831853
/** Hex lattice in world units: x spacing 2 / sqrt(3), z spacing 2 (the z period). */
#define HG_RX 1.1547005
#define HG_R vec2(HG_RX, 2.0)
/** Cell apothem: the flat sides sit at x = +-HG_RX / 2. */
#define HG_APO 0.5773503
/** Cells along z before the hash repeats; divides 8192. */
#define HG_ZWRAP 1024.0
/** Kick lift, world units. */
#define HG_KICK 0.7
#define HG_BUBBLE 0.7

float gFloor;
float gWarp;
float gKick;
float gKickGain;
float gLevel;
float gTurb;
float gRad;
float gBev;
vec3 gEye;
/** Shape inputs (see the header): column height at rest, the walls' extra height, a full band's rise, fog. */
float sBase;
float sWallH;
float sRise;
float sFog;

// Hit globals, restored by one map() call after the normal taps.
float gBand;
float gE;
float gTop;
float gHd;
float gCeil;

float hexDist(vec2 p) {
    p = abs(p);
    return max(dot(p, vec2(0.5, 0.8660254)), p.x);
}

/** TONE: the scale of the hall's width, 1 at rest. */
float gWide;

float wallAt(float ax) {
    // Monotone in ax at every width, which maxTop's neighbour bound relies on.
    return smoothstep(1.2 * gWide, 5.5 * gWide, ax);
}

/** The tallest a column centred at |x| = ax could stand, everything maxed. */
float maxTop(float ax) {
    float w = wallAt(ax);
    return sBase + sWallH * w + sRise * 1.5 * (0.35 + 0.65 * w) + HG_KICK * gKickGain + gTurb * 1.3;
}

/** One column: base on the plane y = gFloor, cap at gFloor + top, in local y. */
float column(float y, float hd, float top) {
    float side = hd - gRad;
    float cap = y - (gFloor + top);
    float chamfer = (side + cap + gBev) * 0.7071068;
    return max(max(side, cap), chamfer);
}

float mapField(vec3 pw) {
    vec3 p = pw;
    // Warp: the hall twists about its axis, slowly along z (period 64).
    p.xy = rot2(gWarp * sin(p.z * (HG_TAU / 64.0) + uTime * 0.2)) * p.xy;

    vec2 q = p.xz;
    vec2 h = HG_R * 0.5;
    vec2 a = mod(q, HG_R) - h;
    vec2 b = mod(q - h, HG_R) - h;
    vec2 gv = dot(a, a) < dot(b, b) ? a : b;
    vec2 cc = q - gv;
    int ix = int(floor(cc.x / (HG_RX * 0.5) + 0.5));
    // Centres sit on whole z; the eye is rebased into 0..1024, so this wrap
    // matches the rebase and the cells never re-hash under the eye.
    int iz = int(mod(floor(cc.y + 0.5), HG_ZWRAP));
    uint u = uhash3(uvec3(ivec3(ix, 7, iz)));
    float band = uhashUnit(u);
    uint u2 = uhash(u);
    float hF = uhashUnit(u2);
    float hC = uhashUnit(uhash(u2));

    float hd = hexDist(gv);
    float ax = abs(cc.x);
    float wall = wallAt(ax);
    // The flat style's cell energy, one texture read per tap, shared above and below.
    float e = clamp(sndSpectrum(band) * (0.5 + gLevel), 0.0, 1.5);
    float kick = gKick * exp(-abs(cc.y - gEye.z - 3.0) * 0.2);
    float grow = sRise * e * (0.35 + 0.65 * wall) + HG_KICK * kick * (1.0 - 0.6 * wall);
    float bob = gTurb * sin(uTime * (0.9 + 1.7 * hF) + hF * 40.0) * (0.3 + wall);
    float topF = sBase + sWallH * wall * (0.4 + 0.6 * hF) + grow + bob;
    float topC = sBase + sWallH * wall * (0.4 + 0.6 * hC) + grow
               + gTurb * sin(uTime * (0.9 + 1.7 * hC) + hC * 40.0) * (0.3 + wall);

    float dF = column(p.y, hd, topF);
    float dC = column(-p.y, hd, topC);

    // Columns further out may be taller than this one: a neighbour two
    // lattice steps out bounds every column the next step can reach.
    float hiN = gFloor + maxTop(ax + 2.0 * HG_RX);
    float toEdge = HG_APO - hd + 0.02;
    float oF = min(dF, max(toEdge, p.y - hiN));
    float oC = min(dC, max(toEdge, -p.y - hiN));

    gCeil = oC < oF ? 1.0 : 0.0;
    gTop = gCeil > 0.5 ? topC : topF;
    gBand = band;
    gE = e;
    gHd = hd;

    float d = min(oF, oC);
    d = min(d, min(p.y - gFloor, -p.y - gFloor));
    return max(d, HG_BUBBLE - length(pw - gEye));
}

/** mapField through lib_shell's hooks: exactly mapField(p) at rest, and mapField read once, in a loop the compiler cannot unroll. */
float mapFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 7; i++) {
        if (shellPass3(i)) r = shellKeep3(i, mapField(shellAt3(i)));
    }
    return r;
}

/** The hall as the shell sees it: spawn, fractal, front and back route through here (identity at rest). */
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
    sBase = shellMap(shellKnob(4), 0.04, 0.12, 0.4);
    sWallH = shellMap(shellKnob(4), 1.4, 2.2, 3.0);
    sRise = shellMap(shellKnob(4), 0.8, 1.3, 1.8);
    gFloor = -shellMap(shellKnob(5), 2.05, 2.5, 2.95);
    gWide = shellMap(shellKnob(3), 1.25, 1.0, 0.75);
    sFog = shellMap(shellKnob(5), 0.1, 0.06, 0.03);
    gRad = HG_APO * shellMapS(clamp(uMorph, 0.0, 1.0), 1.25, 0.9, 0.55);
    gBev = shellMapS(clamp(uMorph, 0.0, 1.0), -0.15, 0.05, 0.25);
    gWarp = shellMapS(clamp(uWarp, 0.0, 1.0), -0.35, 0.0, 0.35);
    gTurb = shellMapS(clamp(uTurbulence / 1.5, 0.0, 1.0), -0.525, 0.0, 0.525);

    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024, so this shift is
    // invisible and exact (power of two). Without it, far down the 8192
    // travel a highp z has ~0.001 of fraction left and the cells speckle.
    ro.z = mod(ro.z, HG_ZWRAP);
    gEye = ro;
    // The grow hooks (lib_shell) open round this eye, in the rebased frame the world is read in.
    shellSetup3(ro, 3.0, HG_APO, false);

    float k0 = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float k1 = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float k2 = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float k3 = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float k4 = clamp(uKnob[4], 0.0, 1.0) - 0.5;

    gKickGain = 1.0 + 0.8 * k0;
    gKick = clamp(SND_KICK_ENV, 0.0, 1.0) * gKickGain;
    gLevel = sndLevel(SND_VOL_ATT);

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float high = sndShaped(SND_HIGH);
    float hueBase = 0.22 * clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0) + 0.3 * k3;

    // The twist's z rate times the widest hall radius (~7) bounds the extra
    // gradient Warp adds; the step shrinks by it.
    float stepScale = 0.85 / (1.0 + gWarp * (HG_TAU / 64.0) * 7.0);

    float t = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < HG_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        float d = map(ro + rd * t);
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * stepScale;
        if (t > HG_FAR) break;
    }

    // Fog: dark, with a glow down the far end of the hall.
    float ahead = pow(max(rd.z, 0.0), 10.0);
    vec3 fogCol = pal(hueBase + 0.6) * (0.03 + 0.2 * ahead) + vec3(0.003, 0.003, 0.006);
    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        float occ = 0.0;
        float sc = 1.0;
        for (int k = 0; k < 2; k++) {
            float hk = 0.12 + 0.2 * float(k);
            occ += (hk - map(p + n * hk)) * sc;
            sc *= 0.6;
        }
        float ao = clamp(1.0 - 1.5 * occ, 0.25, 1.0);
        // Restore the hit cell's globals after the normal and AO taps.
        map(p);

        // The column's own frame: the ceiling is the floor mirrored.
        vec3 pr = p;
        pr.xy = rot2(gWarp * sin(p.z * (HG_TAU / 64.0) + uTime * 0.2)) * pr.xy;
        float yl = gCeil > 0.5 ? -pr.y : pr.y;
        float capY = gFloor + gTop;

        float dif = max(dot(n, -rd), 0.0);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 32.0);
        float fres = pow(1.0 - dif, 3.0);
        vec3 tint = pal(gBand + hueBase);

        bool onColumn = gHd < gRad + 0.03 && yl > gFloor + 0.01;
        if (!onColumn) {
            // The slab between the columns: dark, lit only by what stands on it.
            col = pal(hueBase + 0.55) * 0.04 * ao * (0.4 + dif);
        } else if (yl > capY - gBev - 0.02) {
            // The cap: glows with its band, rim bright with the highs.
            float rimW = 0.05 * (1.0 + 1.2 * k1) + 0.5 * gBev;
            float rim = 1.0 - smoothstep(0.0, rimW, gRad - gHd);
            col = tint * (0.04 + 0.9 * gE * (1.0 + 0.8 * k4)) * (0.5 + 0.5 * dif)
                + pal(gBand + hueBase + 0.05) * rim * (0.25 + high) * 1.3;
        } else {
            // The shaft: dark glass, a glow band just under the cap.
            float band = 1.0 - smoothstep(0.0, 0.35 * (1.0 + 0.8 * k2), capY - yl);
            col = pal(gBand + hueBase + 0.5) * 0.06 * (0.3 + dif) * ao
                + tint * band * gE * 0.45 * ao;
        }
        col += mix(vec3(1.0), tint, 0.5) * (spec * 0.35 + fres * 0.08) * ao;
        col = mix(col, fogCol, 1.0 - exp(-hitT * sFog));
    }

    col *= 0.8 + 0.25 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
