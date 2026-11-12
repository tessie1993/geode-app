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

// Truchet Tubes: the glossy pipes, now a whole 3D maze of them, flown through.
// The flat style laid Smith's quarter arcs on a floor; this is the same idea
// one dimension up. Space is cut into cubes, and every cube holds three
// quarter-torus pipes, each bent around one of the cube's edges and joining
// the centres of the two faces beside that edge. The three edges are chosen so
// that every one of the six faces is met by exactly one pipe, at its centre,
// square to the face; whichever way a neighbour's cube is flipped, its pipe
// starts exactly where this one ends. So the lattice fills with endless,
// smoothly bending pipes that nobody drew, looping through each other in all
// three directions, and the eye threads between them.
//
// THE TILE. One layout (pipes around the edges at (y, z) = (+, +), (x, z) =
// (-, -) and (x, y) = (+, -)) and its eight mirror images, picked by a sign
// flip per axis from the cell's hash. A mirror keeps every face used exactly
// once, so all eight are legal tiles. Colour never comes from the cell id (the
// family's rule: that would draw the grid the pipes hide); it is a smooth field
// of position whose z terms are periodic in the wrap, as is everything else.
//
// ---- camera -----------------------------------------------------------------
//
// camFly: the rig's path through the lattice, its dolly, look, shake and FOV.
// Cubes are TT_CELL = 2 world units and the hash's z index wraps at 64 cells,
// so the maze repeats every 128 units, which divides the rig's 8192 travel
// wrap: the eye's z is rebased by 128 every frame (exact, a power of two) and
// neither the rebase nor the travel wrap ever shows. The Chaos bend's z terms
// have periods 32 and 64, the colour field's 64 and 128, all dividing 128.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets rewire the maze, as before: a scattered third of the cubes re-roll
// their tile on the hits (truchetRoll, floor(SND_HIT_TIME)), and over the onset
// envelope the old pipes melt into the new ones (the two tiles' distances are
// blended), so circuits unplug and re-plug around the eye. The bass
// (sndShaped(SND_LOW)) swells the pipes, under Energy and k0 PULSE. A light
// circles slowly overhead on its own clock; the treble (SND_TREBLE_PRESENCE)
// tightens the highlight. Brightness follows SND_INTENSITY only. The key leans
// the hue (uKeyHue, uKeyStrength).
//
// ---- macros and knobs --------------------------------------------------------
//
// Speed: uTime (the light's circling, the hue drift, the bend's drift).
// Complexity: uMorph raises collars along the pipes (three per quarter turn,
// so they meet at every face) and slims the pipes a little. Chaos: uWarp and
// uTurbulence bend space before it is tiled, so the pipes meander but stay
// joined; uTwist twists the view in view(). Energy: uIntensity scales the bass
// swell (truchetEnergy). Colour: pal(). Space: the camera rig.
// Knobs, offsets from 0.5 (so off is the tuned look): k0 PULSE the bass swell,
// k1 TEXTURE the collars, k2 FLOW the light's circling, k3 TONE the hue,
// k4 BUILD the glow down the maze, k5 SPACE the pipes' girth (more room).
//
// ---- why the distance estimate is safe ----------------------------------------
//
// Each cube measures only its own three pipes (exact torus distances). A
// neighbour's pipe that is not the continuation of one of ours lies at least
// half a cell minus its radius from the shared face (every pipe hugs an edge
// of its own cube and touches only its two face centres), so the distance is
// clamped to faceDist + 0.5 - radius, a true lower bound for those. Blending
// two tiles' distances during a re-roll is a convex mix of 1-Lipschitz fields,
// still 1-Lipschitz. The collars change the radius along the pipe by at most
// ~0.8 per unit of arc length (c^3 of a cosine, see collar()), and the Chaos
// bend stretches space by at most 1 + 0.3a + 1.6b; the field is divided by the
// product of both bounds (gLip). A sphere around the eye is carved out of
// every pipe, so the eye is never inside one.

#define TT_MAX_STEPS 96
#define TT_TAU 6.2831853
/** World units per cube. */
#define TT_CELL 2.0
/** Cubes along z before the hash repeats. Even, so parity-free; 64 * 2 = 128 divides 8192. */
#define TT_ZCELLS 64.0
/** Rebase period of the eye's z: TT_CELL * TT_ZCELLS. */
#define TT_ZWRAP 128.0
#define TT_FAR 34.0
#define TT_BUBBLE 0.75
/** Collars per quarter turn of a pipe: a whole number, so both ends of an arc sit on a crest. */
#define TT_COLLARS 3.0

vec3 gEye;
float gRad;
float gCollar;
float gWarpA;
float gWarpB;
float gLip;
float gHits;
float gEnv;
float gT;
/** Set by map(): the collar profile under the point, 0..1 (1 on a collar). */
float gC;

/**
 * Distance (cell units) from f, the position in a cube (-0.5..0.5), to the
 * nearest of its tile's three pipe centrelines; h picks the mirror image.
 * `ang` is the angle round that pipe's bend, 0..pi/2 from one face to the other.
 */
float tileCentre(vec3 f, float h, out float ang) {
    int b = int(clamp(floor(h * 8.0), 0.0, 7.0));
    vec3 s = vec3(float(b & 1), float((b >> 1) & 1), float((b >> 2) & 1)) * 2.0 - 1.0;
    vec3 g = f * s;
    // Each w points from the point to its pipe's bend centre (a cube edge),
    // so both components are >= 0 inside the cube and the angle stays in the
    // first quadrant, far from atan's seam.
    vec2 wa = vec2(0.5) - g.yz;
    vec2 wb = g.xz + vec2(0.5);
    vec2 wc = vec2(0.5 - g.x, g.y + 0.5);
    float ca = length(vec2(length(wa) - 0.5, g.x));
    float cb = length(vec2(length(wb) - 0.5, g.y));
    float cc = length(vec2(length(wc) - 0.5, g.z));
    vec2 w = wa;
    float c = ca;
    if (cb < c) {
        c = cb;
        w = wb;
    }
    if (cc < c) {
        c = cc;
        w = wc;
    }
    w = max(w, vec2(0.0));
    ang = atan(w.y, max(w.x, 1e-5));
    return c;
}

/**
 * The collar profile at bend angle `ang`: c^3 of a cosine with TT_COLLARS
 * crests per quarter turn, a crest at both faces. Its slope is at most 0.65
 * per radian of phase; the phase turns 4 * TT_COLLARS per radian of bend,
 * i.e. 24 per cell unit of arc at the centreline, ~40 at the inner side of
 * the pipe, so with gCollar <= 0.02 cells the radius slopes <= 0.52 along it.
 */
float collar(float ang) {
    float c = 0.5 + 0.5 * cos(ang * 4.0 * TT_COLLARS);
    return c * c * c;
}

float map(vec3 p) {
    // Chaos: bend space before it is tiled. z terms have periods 32 and 64.
    vec3 q = p;
    if (gWarpA + gWarpB > 1e-4) {
        q.x += gWarpA * sin(p.z * (TT_TAU / 32.0) + gT * 0.21) + gWarpB * sin(p.y * 1.13 - gT * 0.37 + 2.1);
        q.y += gWarpA * sin(p.z * (TT_TAU / 64.0) - gT * 0.17 + 1.3) + gWarpB * sin(p.x * 1.07 + gT * 0.33);
    }
    vec3 P = q / TT_CELL;
    vec3 cell = floor(P);
    vec3 f = P - cell - 0.5;
    vec3 id = vec3(cell.xy, mod(cell.z, TT_ZCELLS));
    // truchetRoll takes a 2D id: fold z into x (|x| stays far under 67 on the rig's paths).
    vec3 roll = truchetRoll(vec2(id.x + 67.0 * id.z, id.y), gHits, gEnv, 3.0);

    float angN;
    float dN = tileCentre(f, roll.y, angN);
    float d = dN;
    float ang = angN;
    if (roll.x != roll.y && roll.z < 1.0) {
        // Old pipes melt into the new: a convex mix of two exact fields.
        float angO;
        float dO = tileCentre(f, roll.x, angO);
        d = mix(dO, dN, roll.z);
        ang = roll.z < 0.5 ? angO : angN;
    }
    float c = collar(ang);
    gC = c;
    float rad = gRad + gCollar * c;
    float dc = d - rad;
    // Pipes from neighbouring cubes that do not continue ours stay half a
    // cell minus their radius beyond the nearest face.
    vec3 af = abs(f);
    float faceDist = 0.5 - max(af.x, max(af.y, af.z));
    dc = min(dc, faceDist + 0.5 - (gRad + gCollar) - 0.02);
    float dw = dc * TT_CELL / gLip;
    return max(dw, TT_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** The smooth colour field, about -1..1, in cell units; its z terms are periodic in the wrap. */
float tubeField(vec3 P, float t) {
    float a = sin(P.x * 0.23 + P.y * 0.19 + 1.7 * sin(P.z * (TT_TAU / 64.0) + t * 0.05) + t * 0.03);
    float b = sin(P.z * (TT_TAU / 32.0) - P.x * 0.07 + P.y * 0.11 - t * 0.04 + 1.9);
    return 0.5 * (a + b);
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
    float morph = truchetMorph();
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.5);
    float treble = clamp(SND_TREBLE_PRESENCE, 0.0, 1.2);

    gT = t;
    gHits = floor(SND_HIT_TIME);
    gEnv = SND_ONSET_ENV;
    gWarpA = 0.8 * clamp(uWarp, 0.0, 2.0);
    gWarpB = 0.18 * clamp(uTurbulence, 0.0, 2.0);
    // Pipe radius in cell units: at most 0.115 + 0.05 + 0.035 * 1.5 * 2 * 1.5 ~ 0.32
    // with the collar, under the 0.5 a pipe may have before it meets its own bend centre.
    float girth = clamp(1.0 - 0.6 * kSpace, 0.6, 1.4);
    gRad = (0.115 - 0.02 * morph) * girth + 0.035 * bass * e * clamp(1.0 + kPulse, 0.5, 1.5);
    // Capped at 0.02 cells, the bound collar() is proved for.
    gCollar = min(0.012 * morph * clamp(1.0 + 1.6 * kTexture, 0.0, 1.8), 0.02);
    gLip = sqrt(1.0 + 0.52 * 0.52) * (1.0 + 0.3 * gWarpA + 1.6 * gWarpB);

    vec3 ro;
    vec3 rd;
    // The mids turn the look aside (lib_truchet's truchetTurn).
    camFly(uv - vec2(truchetTurn(), 0.0), ro, rd);
    // Every z term tiles with a period dividing 128, so this shift is exact
    // and invisible, and z keeps its precision far down the travel.
    ro.z = mod(ro.z, TT_ZWRAP);
    gEye = ro;

    float tm = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < TT_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        float d = map(ro + rd * tm);
        float eps = 0.0012 + 0.0008 * tm;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.9, eps);
        if (tm > TT_FAR) break;
    }

    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.12 * kTone;
    float ahead = pow(max(dot(rd, normalize(uCamBasis[2])), 0.0), 8.0);
    vec3 fogCol = mix(pal(hue + 0.7) * 0.03, pal(hue + 0.78) * 0.07, 0.5 + 0.5 * rd.y)
                + pal(hue + 0.6) * (0.07 + 0.06 * swell) * clamp(1.0 + 1.4 * kBuild, 0.2, 2.2) * ahead;
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        float occ = 0.0;
        float sc = 1.0;
        for (int k = 0; k < 2; k++) {
            float hk = 0.08 + 0.14 * float(k);
            occ += (hk - map(p + n * hk)) * sc;
            sc *= 0.6;
        }
        float ao = clamp(1.0 - 1.8 * occ, 0.3, 1.0);
        // Restore the hit point's globals after the normal and AO taps.
        map(p);

        float lt = t * 0.17 * clamp(1.0 + 1.2 * kFlow, 0.2, 2.2);
        vec3 L = normalize(vec3(0.55 * cos(lt), 0.85, 0.55 * sin(lt)));
        float dif = max(dot(n, L), 0.0);
        // A headlight on the eye, falling off down the maze.
        float head = max(dot(n, -rd), 0.0) / (1.0 + 0.02 * hitT * hitT);
        vec3 h = normalize(L - rd);
        float spec = pow(max(dot(n, h), 0.0), 28.0 + 40.0 * treble);
        float rim = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        float field = tubeField(p / TT_CELL, t);
        vec3 base = pal(hue + 0.1 + 0.2 * field);
        vec3 pipe = base * (0.08 + 0.6 * dif + 0.35 * head) * ao
                  + vec3(1.0, 0.97, 0.92) * spec * 0.55 * (0.4 + 0.6 * ao)
                  + pal(hue + 0.5) * rim * 0.25;
        // The collars catch a little more light: the Complexity detail reads.
        pipe *= 1.0 + 0.25 * gC * morph;
        col = mix(pipe, fogCol, 1.0 - exp(-hitT * 0.06));
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv) * uZoom));
    fragColor = vec4(grade(col), 1.0);
}
