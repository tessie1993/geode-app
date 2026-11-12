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

// Truchet Quad: a square corridor flown through, its four walls tiled with the
// multiscale Truchet in relief. Cells split into four by hash, up to two
// times, so tiles of three sizes sit side by side, and the bright colour of
// the old flat style now stands proud of the wall as a raised enamel band
// while the dark ground is sunk behind it. Because the pattern joins
// seamlessly across every border, big tile to small, the raised paths run
// unbroken along the whole corridor. A headlight rides the eye, a lamp hangs
// a few metres ahead and sways, and the far end of the bore glows in the
// band colour through the fog.
//
// HOW THE SIZES JOIN (unchanged from the flat style). A plain quarter tile's
// edge reads one way from corner to corner, and two half-size tiles side by
// side can never read the same. So the tiles are filled bands: each tile is a
// ground with a band a third of its size wide round two opposite corners, and
// every level SWAPS the two colours. The only places a tile's border disagrees
// with its half-size neighbours' are near the points where the smaller tiles'
// corners land on its edge; each such point - every vertex of the tiling -
// gets a disc of radius one sixth of the size of the grid it first appears
// on, in that level's band colour, and the discs fill exactly the stretches
// that disagreed. (The disc idea is Christopher Carlson's multiscale Truchet;
// the geometry here is worked out from it, not taken from anyone's code.)
// Colour 1 is the same colour at every level, so "height = colour" is one
// continuous relief across all three sizes.
//
// ---- camera ------------------------------------------------------------------
//
// camFly: the eye rides the rig's path down the corridor's +z axis, so travel,
// look, dolly, banking and shake are the Camera tab's. The corridor is 8.8
// units wide, wider than the glide path's usual wander, and it is a shell: an
// eye that strays outside sees a lit box from the outside, never black.
// The eye's z is rebased by TQ_ZWRAP (1024). Every z term has a period that
// divides it: the tiles are TQ_CELL = 0.5 cells per unit, and every level's
// cell ids wrap along z at 512 * 2^level cells (all = 1024 units); the Chaos
// bend and the hue field run at 8, 32 and 64 cells. 1024 divides the rig's
// 8192 travel wrap, so neither the rebase nor the wrap ever shows.
//
// ---- audio -------------------------------------------------------------------
//
// Onsets (SND_ONSET_COUNT, SND_ONSET_ENV) re-roll a scattered third of the
// tiles at every size, cross-fading each band onto the other diagonal, so the
// raised paths re-route along the walls on the hits. The bass
// (SND_BASS_PRESENCE) raises the bands - the old style's breathing, now in
// depth - scaled by Energy. The kick (SND_KICK_ENV) lifts them further just
// ahead of the eye, a swell the camera flies into. That is geometry; nothing
// gets brighter on a hit. The key leans the hue. Brightness and the far glow
// follow SND_ENERGY_TREND only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uTime drives the lamp, the bend and the hue field. Complexity: uMorph
// raises how often cells split, so the walls fill with the small sizes.
// Chaos: uWarp/uTurbulence shear the walls' plan before tiling (z-periodic, see
// above), so the paths meander but stay joined; uTwist bends the view.
// Energy: uIntensity scales the bass rise. Colour: both inks are pal(),
// drifting with a smooth field. Space: the camera rig.
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the kick swell,
// k1 TEXTURE the relief height, k2 FLOW the lamp's sway, k3 TONE the hue,
// k4 BUILD the far glow, k5 SPACE the corridor's width.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// Inside the corridor the free distance to the nearest wall's relief slab is
// the exact plane gap (A - Hmax) - max(|x|, |y|), and the march uses it alone
// until it is within TQ_NEAR of the slab, so the tiling is only evaluated
// close to a wall. There, the field is the wall's height gap divided by
// gLip = sqrt(1 + slope^2): the colour is a smoothstep of half-width TQ_W over
// distance-like edges (each of gradient 1 per cell), nested discs at most
// double that, so the colour's slope is at most 1.5 / TQ_W per cell; times the
// height, TQ_CELL, the Chaos shears' stretch (each shear x += f(y) is
// invertible and stretches by at most 1 + |f'|, multiplied together in main)
// and a margin for the kick's slow z falloff. Near an edge of the square both
// walls are evaluated and the nearer kept. The outer face is a Chebyshev box
// gap, a lower bound on the true distance. A sphere around the eye is carved
// out of everything, so the eye is never inside a wall.

#define TQ_MAX_STEPS 112
#define TQ_FAR 60.0
/** Rebase period of the eye's z; divides 8192. */
#define TQ_ZWRAP 1024.0
/** Level-0 tiles per world unit (a power of two, so the wraps stay whole). */
#define TQ_CELL 0.5
/** Level-0 cells along z before the ids repeat: TQ_ZWRAP * TQ_CELL. */
#define TQ_IDWRAP 512.0
/** Half-width of the corridor at rest, world units. */
#define TQ_HALF 4.4
/** Thickness of the wall shell behind the relief. */
#define TQ_SHELL 0.35
/** Half-width of the relief's bevel, in cells. */
#define TQ_W 0.035
/** Distance from the slab at which the tiling starts being evaluated. */
#define TQ_NEAR 0.05
#define TQ_BUBBLE 0.8

float gP1;
float gP2;
float gHits;
float gT;
float gA;
float gH0;
float gKickH;
float gHmax;
float gLip;
vec3 gEye;
/** Set by map(): the wall the nearest term came from, its (u, z), and the gap to the corridor's edge. */
float gWall;
vec2 gUZ;
float gCorner;

/** The leaf tile holding q: xy = its lower-left corner, z = its level (size 2^-level). Ids wrap along z. */
vec3 leafAt(vec2 q) {
    vec2 c0 = floor(q);
    if (truchetHash3(vec3(c0.x, mod(c0.y, TQ_IDWRAP), 3.0)) >= gP1) return vec3(c0, 0.0);
    vec2 c1 = floor(q * 2.0);
    if (truchetHash3(vec3(c1.x, mod(c1.y, 2.0 * TQ_IDWRAP), 5.0)) >= gP2) return vec3(c1 * 0.5, 1.0);
    return vec3(floor(q * 4.0) * 0.25, 2.0);
}

/** The coarsest grid a vertex lies on (its coordinates are exact quarters). */
float nativeLevel(vec2 v) {
    if (all(equal(fract(v), vec2(0.0)))) return 0.0;
    if (all(equal(fract(v * 2.0), vec2(0.0)))) return 1.0;
    return 2.0;
}

/** Band coverage in a tile: f is 0..1 across it, s its size, w the bevel half-width in cells. */
float bandMask(vec2 f, float bit, float s, float w) {
    vec2 g = bit > 0.5 ? vec2(1.0 - f.x, f.y) : f;
    float e = 1.0 / 6.0 - min(abs(length(g) - 0.5), abs(length(g - 1.0) - 0.5));
    return smoothstep(-w, w, e * s);
}

/** Paint the disc of a tiling vertex of grid level k over the ink. */
float disc(float ink, vec2 p, vec2 v, float k, float w) {
    float r = exp2(-k) / 6.0;
    return mix(ink, 1.0 - mod(k, 2.0), 1.0 - smoothstep(-w, w, length(p - v) - r));
}

/** The multiscale tiling's colour at p (cells): 0 the sunk ground, 1 the raised band. */
float quadInk(vec2 p, float w) {
    vec3 leaf = leafAt(p);
    float lv = leaf.z;
    float s = exp2(-lv);
    vec2 f = (p - leaf.xy) / s;

    vec2 rid = vec2(leaf.x, mod(leaf.y, TQ_IDWRAP)) * 4.0 + vec2(lv * 4099.0, 0.0);
    vec3 roll = truchetRoll(rid, gHits, SND_ONSET_ENV, 3.0);
    float m = bandMask(f, step(0.5, roll.y), s, w);
    if (step(0.5, roll.x) != step(0.5, roll.y) && roll.z < 1.0) {
        m = mix(bandMask(f, step(0.5, roll.x), s, w), m, roll.z);
    }
    // Even levels are ground 0 with band 1; odd levels the other way round.
    float ground = mod(lv, 2.0);
    float ink = mix(ground, 1.0 - ground, m);

    // The leaf's own corners.
    for (int k = 0; k < 4; k++) {
        vec2 v = leaf.xy + s * vec2(float(k - 2 * (k / 2)), float(k / 2));
        ink = disc(ink, p, v, nativeLevel(v), w);
    }

    // Finer vertices on the nearest edge, where a subdivided neighbour's
    // corners land: the edge midpoint and, for the biggest tiles, the nearer
    // quarter point. Each exists only if the neighbour across is split that
    // far; the lookup lands just inside it.
    if (lv < 1.5) {
        vec2 de = min(f, 1.0 - f);
        bool vert = de.x < de.y;
        float along = vert ? f.y : f.x;
        vec2 axis = vert ? vec2(0.0, 1.0) : vec2(1.0, 0.0);
        vec2 nrm = vert ? vec2(f.x < 0.5 ? -1.0 : 1.0, 0.0) : vec2(0.0, f.y < 0.5 ? -1.0 : 1.0);
        vec2 e0 = leaf.xy + s * (vert ? vec2(step(0.5, f.x), 0.0) : vec2(0.0, step(0.5, f.y)));
        vec2 nudge = nrm * 0.01 + axis * 0.01;
        vec2 vm = e0 + axis * (0.5 * s);
        if (leafAt(vm + nudge).z > lv + 0.5) ink = disc(ink, p, vm, lv + 1.0, w);
        if (lv < 0.5) {
            vec2 vq = e0 + axis * ((along < 0.5 ? 0.25 : 0.75) * s);
            if (leafAt(vq + nudge).z > 1.5) ink = disc(ink, p, vq, 2.0, w);
        }
    }
    return ink;
}

/**
 * A wall's (u, z) as tiling cells, sheared by Chaos. Each wall is offset by a
 * whole number of cells so the four never show the same tiles. The shears'
 * z frequencies are 8 and 32 cells, which divide the 512-cell id wrap.
 * Identity at default.
 */
vec2 wallCell(float wall, vec2 uz) {
    vec2 c = vec2(uz.x * TQ_CELL + 19.0 * wall, uz.y * TQ_CELL);
    float a = 0.8 * clamp(uWarp, 0.0, 2.0);
    float b = 0.18 * clamp(uTurbulence, 0.0, 2.0);
    if (a + b <= 1e-4) return c;
    c.x += a * sin(c.y * (TRUCHET_TAU / 32.0) + gT * 0.21);
    c.y += a * 0.5 * sin(c.x * 0.29 - gT * 0.17 + 1.3);
    c.x += b * sin(c.y * (TRUCHET_TAU / 8.0) - gT * 0.37 + 2.1);
    c.y += b * 0.6 * sin(c.x * 1.07 + gT * 0.33);
    return c;
}

/** A smooth hue field over the walls, about -1..1; z periods 32 and 64 cells. */
float quadField(vec2 c) {
    float a = sin(c.x * 0.23 + 1.7 * sin(c.y * (TRUCHET_TAU / 64.0) + gT * 0.05) + gT * 0.03);
    float b = sin(c.y * (TRUCHET_TAU / 32.0) - c.x * 0.07 - gT * 0.04 + 1.9);
    return 0.5 * (a + b);
}

/** Relief height (world units) of a wall at (u, z): the bass rise, plus the kick swell just ahead of the eye. */
float wallHeight(float wall, vec2 uz) {
    float hz = gH0 + gKickH * exp(-abs(uz.y - gEye.z - 6.0) * 0.2);
    return hz * quadInk(wallCell(wall, uz), TQ_W);
}

/** Wall 0 floor, 1 ceiling, 2 right, 3 left: which one the coordinate with the larger magnitude faces. */
float wallFor(vec2 q, bool side) {
    return side ? (q.x > 0.0 ? 2.0 : 3.0) : (q.y < 0.0 ? 0.0 : 1.0);
}

/** The along-wall coordinate of q on that wall. */
float wallU(float wall, vec2 q) {
    if (wall < 0.5) return q.x;
    if (wall < 1.5) return -q.x;
    if (wall < 2.5) return q.y;
    return -q.y;
}

float map(vec3 p) {
    vec2 q = p.xy;
    float ax = abs(q.x);
    float ay = abs(q.y);
    bool side = ax > ay;
    float m = max(ax, ay);
    float mo = min(ax, ay);
    gCorner = gA - mo;

    float wall = wallFor(q, side);
    gWall = wall;
    gUZ = vec2(wallU(wall, q), p.z);

    float top = gA - gHmax;
    float d = top - m;
    if (d < TQ_NEAR) {
        d = (gA - wallHeight(wall, gUZ) - m) / gLip;
        // Near an edge of the square the other wall can be the nearer one.
        if (top - mo < TQ_NEAR) {
            float w2 = wallFor(q, !side);
            vec2 uz2 = vec2(wallU(w2, q), p.z);
            float d2 = (gA - wallHeight(w2, uz2) - mo) / gLip;
            if (d2 < d) {
                d = d2;
                gWall = w2;
                gUZ = uz2;
            }
        }
    }
    // The shell's outer face (a Chebyshev gap: never more than the true one).
    d = max(d, m - (gA + TQ_SHELL));
    return max(d, TQ_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 farGlow(vec3 rd, float hue, float swell, float build) {
    vec3 col = pal(hue + 0.62) * 0.03;
    // Both ends of the corridor glow in the band colour, down the z axis.
    float ahead = abs(rd.z);
    col += pal(hue + 0.12) * (0.32 * pow(ahead, 16.0) + 0.05 * ahead * ahead) * (0.75 + 0.35 * swell) * build;
    return col;
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    // The mids turn the look aside (lib_truchet's truchetTurn).
    camFly(uv - vec2(truchetTurn(), 0.0), ro, rd);
    // Every z term tiles with a period dividing 1024 (see the header), so this
    // shift is exact and invisible, and keeps highp z precise far down the travel.
    ro.z = mod(ro.z, TQ_ZWRAP);
    gEye = ro;

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float t = truchetTime();
    gT = t;
    float e = truchetEnergy();
    float swell = clamp(SND_ENERGY_TREND * uSoundDrive, 0.0, 1.5);
    float morph = truchetMorph();
    gHits = floor(SND_ONSET_COUNT);
    gP1 = 0.3 + 0.45 * morph;
    gP2 = 0.2 + 0.5 * morph;
    gA = TQ_HALF * (1.0 + 0.4 * kSpace);

    float reliefGain = 1.0 + 1.2 * kTexture;
    gH0 = (0.1 + 0.07 * clamp(SND_BASS_PRESENCE * uSoundDrive, 0.0, 1.5) * e) * reliefGain;
    gKickH = 0.08 * clamp(SND_KICK_ENV * uSoundDrive, 0.0, 1.0) * (1.0 + 1.6 * kPulse) * e * reliefGain;
    gHmax = gH0 + gKickH;

    // The step bound (see the header): colour slope 1.5 / TQ_W per cell, in
    // world units, through the Chaos shears' stretch, plus the kick falloff.
    float wa = 0.8 * clamp(uWarp, 0.0, 2.0);
    float wb = 0.18 * clamp(uTurbulence, 0.0, 2.0);
    float stretch = (1.0 + 0.197 * wa) * (1.0 + 0.145 * wa) * (1.0 + 0.786 * wb) * (1.0 + 0.642 * wb);
    float slope = gHmax * (1.5 / TQ_W) * TQ_CELL * stretch + 0.2 * gKickH;
    gLip = sqrt(1.0 + slope * slope);

    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.25 * kTone;
    float build = max(0.0, 1.0 + 1.5 * kBuild);

    float tm = 0.02;
    float hitT = -1.0;
    for (int i = 0; i < TQ_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        float d = map(ro + rd * tm);
        float eps = 0.0006 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d, eps * 0.5);
        if (tm > TQ_FAR) break;
    }

    vec3 sky = farGlow(rd, hue, swell, build);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        // Restore the hit wall's globals after the normal taps.
        map(p);
        float wall = gWall;
        vec2 uz = gUZ;
        float corner = gCorner;

        // The colour is redrawn with the bevel widened to the pixel footprint,
        // so far tiles soften instead of shimmering.
        float pix = hitT * 2.0 / (uResolution.y * max(uCamFocal, 0.1));
        vec2 c = wallCell(wall, uz);
        float ink = quadInk(c, max(TQ_W, pix * TQ_CELL * 1.5));
        float field = 0.14 * quadField(c);
        vec3 albedo = mix(pal(hue + 0.62 + field) * 0.12, pal(hue + 0.12 + field) * 0.85, ink);

        vec3 fwd = uCamBasis[2];
        vec3 sideV = uCamBasis[0];
        float sway = (1.0 + 1.6 * kFlow) * t * 0.37;
        vec3 lamp = ro + fwd * 4.5 + sideV * (1.6 * sin(sway)) + uCamBasis[1] * (0.6 * cos(sway * 0.7));
        vec3 head = ro + uCamBasis[1] * 0.25;

        vec3 lampC = mix(vec3(1.0, 0.94, 0.85), pal(hue + 0.12), 0.35);
        float dif = 0.0;
        float spec = 0.0;
        {
            vec3 toL = lamp - p;
            float dl = length(toL);
            vec3 l = toL / dl;
            float at = 1.0 / (1.0 + 0.06 * dl * dl);
            dif += max(dot(n, l), 0.0) * at;
            spec += pow(max(dot(reflect(rd, n), l), 0.0), 32.0) * at;
        }
        {
            vec3 toL = head - p;
            float dl = length(toL);
            vec3 l = toL / dl;
            float at = 0.6 / (1.0 + 0.04 * dl * dl);
            dif += max(dot(n, l), 0.0) * at;
            spec += pow(max(dot(reflect(rd, n), l), 0.0), 32.0) * at;
        }
        // The sunk ground and the corridor's edges sit in shadow.
        float ao = (0.55 + 0.45 * ink) * (0.55 + 0.45 * smoothstep(0.0, 1.6, corner));
        col = albedo * (0.07 + 1.5 * dif) * ao * lampC;
        // The bands are enamel: glossy, with a little light of their own so the
        // pattern still reads down the fog. The ground is matte.
        col += lampC * spec * (0.12 + 0.6 * ink) * ao;
        col += albedo * 0.18 * ink;
        // A fine dark contour where the colour turns, on the bevel.
        col *= 1.0 - 0.35 * 4.0 * ink * (1.0 - ink);
        col = mix(col, sky, 1.0 - exp(-hitT * 0.045));
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
