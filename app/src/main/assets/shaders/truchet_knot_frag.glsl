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

// Truchet Knot: Celtic interlace grown from a Truchet tiling, now a vault of
// real cords flown through. Two knotwork carpets, one below the eye and one
// above it, run to the horizon; every strand is a doubled cord (two round
// cords side by side, a dark groove between them) lying on a dark ground, and
// at every crossing one strand truly arches OVER the other, which dips UNDER
// it. A lantern drifts ahead of the eye between the two carpets, raking the
// cords so their arches catch it and the dips fall into shadow; fog closes the
// far end in the palette's glow.
//
// THE TILING, AS BEFORE. The grid runs at 45 degrees to the flight, so its
// straight runs are the diagonals knotwork is drawn on. Each cell holds a
// CROSSING (two straight strands through the opposite edge midpoints) or one
// of the two quarter-arc tiles. Every strand leaves every tile square to the
// edge at its midpoint, at height zero and with zero slope, so the cords run
// on smoothly whatever the neighbours hold.
//
// WHY THE OVER-UNDER ALWAYS ALTERNATES. At a crossing the cell's checkerboard
// parity picks which strand arches up: in even cells the one running along the
// first axis, in odd cells the other. Follow a strand from one crossing to the
// next with m arc tiles between: the parity flips m + 1 times and the strand's
// axis m times, 2m + 1 flips in all - always odd. So a strand that went over
// goes under at its next crossing, with no planning across cells. The arch is
// A * (1 + cos(2 pi s)) / 2 along the strand (s = -0.5..0.5 across the cell):
// zero, flat, at both edges, highest at the crossing.
//
// ---- camera ------------------------------------------------------------------
//
// camFly: the eye rides the rig's path in x and z, held at the vault's middle
// height, and looks along the rig's view (look, roll, shake, FOV all apply).
// The eye's z is rebased by KN_ZWRAP = 64, which divides the rig's 8192
// travel wrap. The cell coordinates are c = E + s * R(w - eye), with E the
// eye's cell position at the base scale (0.5 * (x + z, z - x)) and s the
// Complexity scale about the eye; rebasing z by 64 shifts c by exactly (32, 32).
// So everything in cell space keeps a period that divides 32 along both axes:
// the tile key is (id.x - id.y, (id.x + id.y) mod 32) - invariant, and even, so
// the checkerboard parity survives - and the Chaos bend and the hue field have
// period 16. Neither the rebase nor the travel wrap ever shows.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets (SND_ONSET_COUNT, SND_ONSET_ENV) re-roll a scattered third of the cells
// between crossing and turning, cross-fading the two cords' fields, so the
// knot re-ties itself on the hits. The snare (SND_SNARE_ENV) lifts the arches
// and deepens the dips, opening the gaps at the crossings for a moment. The
// bass (SND_BASS_PRESENCE, through truchetBreath) breathes the vault: the two
// carpets close in toward the eye on a low end. The key leans the hue.
// Brightness follows SND_ENERGY_TREND only. The kick reaches it through the
// rig's dolly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uTime drives the hue drift, the lantern and the bend. Complexity:
// uMorph shrinks the cells (scaled about the eye, so it never slides the
// floor away). Chaos: uWarp/uTurbulence bend the knot before tiling (a
// wrap-safe bend, see the camera); uTwist in view(). Energy: uIntensity scales
// the snare arches and the bass breathing. Colour: cords and ground are pal().
// Space: the camera rig. Knobs (offsets from 0.5, so off is the tuned look):
// k0 PULSE the snare arch, k1 TEXTURE the split between the two cords, k2
// FLOW the lantern's swing, k3 TONE the hue, k4 BUILD the far glow, k5 SPACE
// the vault's height.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// In cell units a cord is length(lateral - offset, height - arch) - radius,
// exact for a straight cord on the flat; the arch only tilts it, with slope at
// most pi * A along the strand, so dividing by sqrt(1 + (pi A)^2) bounds the
// gradient. A cell sees only its own strands (straight lines extended, full
// circles about its corners), which reach the neighbour's cords at the shared
// midpoints, square and flat, so the miss across a border is small; the march
// steps 0.8 of the bound. The Chaos bend stretches the plane by at most
// 1 + 0.39 a + 0.79 b (each axis bent by a sine of the other), and the field is
// divided by that too. A re-roll mixes two tile fields, a convex blend whose
// gradient is no larger than theirs. The ground is an exact plane. Each half of
// the vault sees only its own carpet, so its field is capped by the distance to
// the nearest point the other carpet can reach. The eye sits between the
// carpets, always clear of both; the march starts where a ray enters a carpet's
// slab and lands on the ground plane if it gets through.

#define KN_MAX_STEPS 96
/** Rebase period of the eye's z; divides 8192. */
#define KN_ZWRAP 64.0
/** The eye's height above each carpet's cord plane at rest, world units. */
#define KN_H 1.3
/** The highest the arches go, cell units (sets the ground). */
#define KN_ARCH_MAX 0.27
/** Half-width of a doubled cord, cell units. */
#define KN_HALF 0.155
/** Depth of the ground below the cord plane, cell units: under the deepest dip. */
#define KN_GROUND 0.48
#define KN_FAR 34.0
/** Period of the bend and the hue field in cell units: divides the 32-cell rebase shift. */
#define KN_P 16.0

vec3 gEye;
vec2 gEyeCell;
float gScl;
float gCpw;
float gH;
float gArch;
float gOff;
float gR;
float gLip;
float gWarpA;
float gWarpB;
float gT;
float gHits;
float gEnv;
/** Set by map(): the cell coordinate, height above the cord plane (cells), lateral distance to the nearest strand, on the ground or not. */
vec2 gC;
float gYc;
float gNear;
float gOnGround;

/** Chaos: each axis bent by sines of the other, period KN_P, so the rebase lands on a whole bend. */
vec2 knotWarp(vec2 c) {
    if (gWarpA + gWarpB <= 1e-4) return c;
    const float f1 = TRUCHET_TAU / KN_P;
    const float f2 = TRUCHET_TAU / (KN_P * 0.5);
    c += gWarpA * vec2(sin(c.y * f1 + gT * 0.21), sin(c.x * f1 - gT * 0.17 + 1.3));
    c += gWarpB * vec2(sin(c.y * f2 - gT * 0.37 + 2.1), sin(c.x * f2 + gT * 0.33));
    return c;
}

/** A smooth hue field, about -1..1, period KN_P on both axes: never keyed to the cell. */
float knotField(vec2 c) {
    const float f = TRUCHET_TAU / KN_P;
    float a = sin(c.x * f + 1.7 * sin(c.y * f + gT * 0.05) + gT * 0.03);
    float b = sin(c.y * f - c.x * f - gT * 0.04 + 1.9);
    return 0.5 * (a + b);
}

/** A doubled cord about a strand: lateral distance lat, height above its centreline dy, cell units. */
float cordSdf(float lat, float dy) {
    return length(vec2(abs(lat) - gOff, dy)) - gR;
}

/** The arch along a strand, s = -0.5..0.5 across the cell: 1 at the crossing, 0 and flat at the edges. */
float knotBump(float s) {
    return 0.5 + 0.5 * cos(TRUCHET_TAU * s);
}

/** One tile's cords at cell position f and height yc (cell units); near = lateral distance to the nearest strand. */
float knotTile(vec2 f, float yc, float parity, float kind, out float near) {
    if (kind < 0.5) {
        // Strand A runs along the cell's y (lateral f.x), strand B along x.
        float sA = parity < 0.5 ? 1.0 : -1.0;
        float hA = sA * gArch * knotBump(f.y);
        float hB = -sA * gArch * knotBump(f.x);
        near = min(abs(f.x), abs(f.y));
        return min(cordSdf(f.x, yc - hA), cordSdf(f.y, yc - hB));
    }
    TruchetArc a = truchetQuarter(f, parity, kind - 1.0);
    near = abs(a.d);
    return cordSdf(a.d, yc);
}

float knotKind(float h) {
    return h < 0.5 ? 0.0 : (h < 0.75 ? 1.0 : 2.0);
}

float map(vec3 w) {
    float up = step(0.0, w.y);
    float ay = abs(w.y);
    // Height above this half's cord plane, world units, then cells.
    float yw = gH - ay;
    vec2 d = w.xz - gEye.xz;
    vec2 c = gEyeCell + gScl * 0.5 * vec2(d.x + d.y, d.y - d.x);
    // The ceiling is another knot: offset half a cell, its own hash layer.
    c += 0.5 * up;
    c = knotWarp(c);
    gC = c;
    vec2 id = floor(c);
    vec2 f = c - id - 0.5;
    float diag = id.x + id.y;
    float parity = mod(diag, 2.0);
    vec2 key = vec2(id.x - id.y, mod(diag, 32.0) + 64.0 * up);
    float yc = yw * gCpw;
    gYc = yc;

    vec3 roll = truchetRoll(key, gHits, gEnv, 3.0);
    float kNew = knotKind(roll.y);
    float kOld = knotKind(roll.x);
    float near;
    float dc = knotTile(f, yc, parity, kNew, near);
    if (kOld != kNew && roll.z < 1.0) {
        float nearOld;
        float dOld = knotTile(f, yc, parity, kOld, nearOld);
        dc = mix(dOld, dc, roll.z);
        near = mix(nearOld, near, roll.z);
    }
    gNear = near;
    float dg = yc + KN_GROUND;
    gOnGround = step(dg, dc);
    float dd = min(dc, dg) / (gCpw * gLip);
    // The other half's carpet reaches no nearer the middle than its arch tops.
    float other = ay + gH - (gArch + KN_HALF) / gCpw;
    return min(dd, other);
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

    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(SND_ENERGY_TREND * uSoundDrive, 0.0, 1.5);
    float snare = clamp(SND_SNARE_ENV * uSoundDrive, 0.0, 1.0) * e;

    gT = t;
    gHits = floor(SND_ONSET_COUNT);
    gEnv = SND_ONSET_ENV;
    gScl = 1.0 + 0.6 * truchetMorph();
    gCpw = gScl * 0.70710678;
    gH = KN_H * truchetBreath(SND_BASS_PRESENCE * uSoundDrive) * (1.0 + 0.3 * kSpace);
    gArch = min(0.19 + 0.04 * snare * clamp(1.0 + 1.6 * kPulse, 0.2, 1.8), KN_ARCH_MAX);
    gOff = 0.085 + 0.03 * kTexture;
    gR = KN_HALF - gOff;
    gWarpA = 0.8 * clamp(uWarp, 0.0, 2.0);
    gWarpB = 0.18 * clamp(uTurbulence, 0.0, 2.0);
    float stretch = 1.0 + gWarpA * (TRUCHET_TAU / KN_P) + gWarpB * (TRUCHET_TAU / (KN_P * 0.5));
    gLip = sqrt(1.0 + TRUCHET_PI * gArch * TRUCHET_PI * gArch) * stretch;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The vault's middle; z rebased by a period every cell-space term divides.
    ro = vec3(ro.x, 0.0, mod(ro.z, KN_ZWRAP));
    gEye = ro;
    gEyeCell = 0.5 * vec2(ro.x + ro.z, ro.z - ro.x);

    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.12 * kTone;
    float ahead = pow(max(dot(rd, normalize(uCamBasis[2])), 0.0), 10.0);
    float horizon = exp(-abs(rd.y) * 9.0);
    vec3 fogCol = pal(hue + 0.62) * 0.03
                + pal(hue + 0.3) * (0.05 + 0.1 * swell) * (1.0 + kBuild) * (0.4 * horizon + ahead)
                + vec3(0.003, 0.002, 0.005);
    vec3 col = fogCol;

    // Enter a carpet's slab at its arch tops; past its ground the ray is in it.
    float ady = abs(rd.y);
    float topW = gH - (gArch + KN_HALF + 0.01) / gCpw;
    float botW = gH + KN_GROUND / gCpw;
    float hitT = -1.0;
    float used = 0.0;
    if (ady > 1e-4) {
        float tm = max(topW / ady, 0.03);
        float t1 = min(botW / ady, KN_FAR);
        for (int i = 0; i < KN_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (tm > t1) break;
            used = float(i);
            float d = map(ro + rd * tm);
            float eps = 0.0008 * tm + 0.0005;
            if (d < eps) {
                hitT = tm;
                break;
            }
            tm += max(d * 0.8, eps);
        }
        if (hitT < 0.0 && tm >= t1 && t1 < KN_FAR) hitT = t1;
    }

    // The lantern: ahead of the eye in the vault's middle, drifting side to
    // side and up and down so it rakes both carpets in turn.
    vec3 fwd = normalize(vec3(uCamBasis[2].x, 0.0, uCamBasis[2].z) + vec3(0.0, 0.0, 1e-4));
    vec3 side = vec3(fwd.z, 0.0, -fwd.x);
    // FLOW scales the swing, never the clock: a knob times uTime would jump.
    float drift = t * 0.37;
    vec3 lightPos = ro + fwd * 3.0 + side * (1.6 * (1.0 + 0.8 * kFlow) * sin(drift)) + vec3(0.0, 0.45 * gH * sin(drift * 0.61 + 1.1), 0.0);
    vec3 lightC = mix(vec3(1.0, 0.93, 0.82), pal(hue + 0.15), 0.35);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        // Restore the hit point's globals after the normal taps.
        map(p);

        vec3 toL = lightPos - p;
        float dl = length(toL);
        vec3 l = toL / dl;
        float atten = 1.0 / (1.0 + 0.1 * dl * dl);
        float dif = max(dot(n, l), 0.0);
        float spec = pow(max(dot(reflect(rd, n), l), 0.0), 28.0);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 0.8, 0.4, 1.0);

        vec3 tint = pal(hue + 0.1 + 0.1 * knotField(gC));
        vec3 base;
        if (gOnGround > 0.5) {
            // The ground, in shadow under the cords as the old ground was.
            base = pal(hue + 0.62) * 0.12;
            ao *= 0.35 + 0.65 * smoothstep(KN_HALF, KN_HALF + 0.12, gNear);
            spec *= 0.2;
        } else {
            base = tint;
            // The dips sit in shadow, the arches in the light.
            ao *= 0.45 + 0.55 * smoothstep(-0.3, 0.12, gYc);
        }
        col = base * (0.05 + 1.5 * dif * atten) * ao * lightC;
        col += lightC * spec * atten * 0.55 * ao;
        col += tint * fres * 0.08 * ao;
        col = mix(col, fogCol, 1.0 - exp(-hitT * 0.075));
    }

    // The lantern itself, a soft glow hanging in the vault.
    vec3 toLight = lightPos - ro;
    float along = max(dot(toLight, rd), 0.0);
    float miss = length(toLight - rd * along);
    if (hitT < 0.0 || along < hitT) col += pal(hue + 0.15) * 0.05 / (1.0 + miss * miss * 60.0);

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
