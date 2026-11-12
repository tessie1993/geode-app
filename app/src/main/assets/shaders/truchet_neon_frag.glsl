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

// Truchet Neon: a hall of neon tubing, flown through. The old style drew thin
// neon lines on black; now the lines are real glass tubes lying on a black
// glass floor, and a second tiling hangs from a black glass ceiling, so the
// eye glides between two planes of glowing paths that run off into the fog.
//
// Each floor (and ceiling) cell of NE_CELL world units holds Smith's quarter
// tile, and each quarter arc is three concentric tubes (radius 1/3, 1/2 and
// 2/3 of a cell about its corner), as before. Across an edge the middle tube
// meets a middle tube, and the 1/3 tube meets the neighbour's 2/3 tube, so the
// tubes fall into two families of paths that never touch: the middle ones
// glow in one hue, the outer pair in another, and both drift slowly across
// the hall. Each tube is a hot white-ish core with a halo of its family's
// colour, gathered along the ray as it passes (the bloom is volumetric now,
// so a tube near the eye has a wide halo and a far one a tight line). The
// floor is wet black glass: it mirrors the tubing, which is most of what
// reads as depth. No colour is keyed to the cell id (the family's rule): hue
// comes from a smooth field of position.
//
// ---- camera ------------------------------------------------------------------
//
// camFly: the rig's path runs down +z between the planes; its glide, spiral,
// orbit and hover all stay inside the hall's half-height NE_HALF (the spiral's
// widest swing is 2 units, the orbit circles the origin, which is just another
// spot on two endless planes). The eye's z is rebased by NE_ZWRAP = 64 world
// units = 32 cells: the cell ids wrap mod 32 (even, so the checkerboard holds),
// and every z term of the warp and the hue field has a period of 8, 16 or 32
// cells, so the rebase and the rig's 8192 travel wrap never show.
//
// ---- audio -------------------------------------------------------------------
//
// Onsets (SND_ONSET_COUNT, SND_ONSET_ENV) swing a scattered third of the tiles a
// quarter turn, as before, and a turning tile hops its tubes off the floor on
// the way round, so the tubing re-routes in the air. Geometry only.
// Mids (sndShaped(SND_MIDDLE)) widen the halo, as before. The bass
// (SND_BASS_PRESENCE) breathes the tubes' thickness; it used to breathe the
// tile scale, which a z-periodic world cannot do without a seam. The key
// leans both hues. Brightness follows SND_LEVEL only; nothing flashes on a hit.
//
// ---- macros and knobs --------------------------------------------------------
//
// Speed: uTime drifts the hues and bends. Complexity: uMorph used to shrink the
// tiles; here it threads a third family of tubes between the others (radius
// 5/12 and 7/12, which also meet each other across every edge) and slims all
// of them. Chaos: uWarp / uTurbulence bend the plane before tiling, with
// z-periodic bends; uTwist in view(). Energy: uIntensity scales the hop, the
// halo's mid widening and the bass breathing. Colour: pal(), at full
// saturation. Space: view() and the rig.
// Knobs are offsets from 0.5, so off is the tuned look: k0 PULSE the hop,
// k1 TEXTURE the floor's gloss and the tubes' girth, k2 FLOW the halo width, k3 TONE the hue,
// k4 BUILD the far haze, k5 SPACE the hall's height.
//
// ---- why the distance estimate is safe ---------------------------------------
//
// Within a cell the tubes are exact: a tube is the planar distance to its arc
// (min over both corners and every lane, so a ring from the far corner is
// never missed) lifted into a circle of radius NE_R about the tube's height.
// Neighbours' tubes meet ours at the edge midpoint lanes, square to the edge,
// and a turning tile's arc ends can poke a few hundredths of a cell over the
// edge, so below the highest any tube can reach (tubes plus the full hop) the
// step is also clamped to the distance to the cell's border; above it the
// height alone is a true bound. The plane is bent before tiling by a map whose
// slope is at most 0.91 per axis; every distance is divided by its Lipschitz
// bound (1 + that slope), and the bend's determinant stays above 0.2, so it
// never folds. Only the nearer plane's tiling is evaluated; the far plane is
// bounded by its slab height. A sphere around the eye is carved out of
// everything, so the eye is never inside a tube.

#define NE_MAX_STEPS 96
#define NE_REFL_STEPS 40
#define NE_FAR 48.0
#define NE_REFL_FAR 22.0
/** World units per tile. */
#define NE_CELL 2.0
/** Cells along z before the ids repeat: 32 * NE_CELL = NE_ZWRAP. Even, so the parity holds. */
#define NE_ZCELLS 32.0
/** Rebase period of the eye's z; divides the rig's 8192. */
#define NE_ZWRAP 64.0
/** Half-height of the hall at rest: floor at -NE_HALF, ceiling at +NE_HALF. */
#define NE_HALF 2.8
/** Tube radius at rest, world units. */
#define NE_R 0.07
#define NE_BUBBLE 0.6
#define NE_FOG 0.045

// Per-pixel constants, set once in main().
float gHalf;
float gR;
float gHopAmp;
float gFadeIn;
float gLip;
float gHits;
float gEnv;
float gT;
vec3 gEye;

/**
 * What the field holds at a point (lib_truchet_field): the distance, the three lane families' own
 * distances (middle, outer, inner), the hue field there and the material (0 plane, 1 middle, 2 outer,
 * 3 inner tube).
 */
struct TruchetHit {
    float d;
    float dMid;
    float dOut;
    float dIn;
    float fld;
    float mat;
};

/** Push a palette colour to full saturation: gas in glass, not pastel. */
vec3 neonize(vec3 c) {
    float lo = min(c.r, min(c.g, c.b));
    float hi = max(c.r, max(c.g, c.b));
    return clamp((c - lo) / max(hi - lo, 1e-3), 0.0, 1.0);
}

/**
 * Chaos: lib_truchet's bend, rebuilt with z periods of 16 and 8 cells so it
 * survives the rebase. Both terms read the unbent point, so the Jacobian is
 * [[1, a], [b, 1]] with |a| <= 0.393 A + 0.785 B and |b| <= 0.29 A + 1.07 B
 * (at most 0.91 and 0.85 at the top of both sliders): Lipschitz 1 + max(a, b),
 * determinant 1 - ab > 0.2. Identity at default.
 */
vec2 neonWarp(vec2 q, float t) {
    float a = 0.8 * (2.0 * gTruchetIn.warp);
    float b = 0.18 * (2.0 * gTruchetIn.chaos);
    if (a + b <= 1e-4) return q;
    vec2 o = q;
    q.x += a * sin(o.y * (TRUCHET_TAU / 16.0) + t * 0.21) + b * sin(o.y * (TRUCHET_TAU / 8.0) - t * 0.37 + 2.1);
    q.y += a * sin(o.x * 0.29 - t * 0.17 + 1.3) + b * sin(o.x * 1.07 + t * 0.33);
    return q;
}

/** The hue field, about -1..1, cell units; z periods of 32 and 16 cells. */
float neonField(vec2 q, float t) {
    float a = sin(q.x * 0.23 + 1.7 * sin(q.y * (TRUCHET_TAU / 32.0) + t * 0.05) + t * 0.03);
    float b = sin(q.y * (TRUCHET_TAU / 16.0) - q.x * 0.07 - t * 0.04 + 1.9);
    return 0.5 * (a + b);
}

/** min over both corners of |l - r|: the planar distance to a lane of radius r, cell units. */
float laneDist(float l1, float l2, float r) {
    return min(abs(l1 - r), abs(l2 - r));
}

/**
 * The tubing of one plane. xz is the world point, yRel its height above that
 * plane, idOff an even shift of the cell ids so the ceiling rolls its own
 * tiles. Returns a bound on the distance to the nearest tube in .d, with each
 * family's own distance (dMid, dOut, dIn) for the halo and the hue field.
 */
TruchetHit tubing(vec2 xz, float yRel, float idOff) {
    TruchetHit h;
    vec2 q = neonWarp(xz * (1.0 / NE_CELL), gT);
    h.fld = neonField(q, gT);
    vec2 cell = floor(q);
    vec2 f0 = q - cell - 0.5;
    vec2 id = vec2(cell.x + idOff, mod(cell.y, NE_ZCELLS));

    vec3 roll = truchetRoll(id, gHits, gEnv, 3.0);
    float bitOld = step(0.5, roll.x);
    float bitNew = step(0.5, roll.y);
    float bit = bitNew;
    float hop = 0.0;
    vec2 f = f0;
    if (bitOld != bitNew && roll.z < 1.0) {
        // A quarter turn maps one diagonal onto the other; the tile rises on
        // the way round and is back down when it lands.
        f = truchetRot(-roll.z * TRUCHET_PI * 0.5) * f0;
        bit = bitOld;
        hop = sin(TRUCHET_PI * roll.z);
    }
    vec2 g = vec2(bit > 0.5 ? -f.x : f.x, f.y);
    float l1 = length(vec2(0.5) - g);
    float l2 = length(g + vec2(0.5));

    float pm = laneDist(l1, l2, 0.5) * NE_CELL;
    float po = min(laneDist(l1, l2, 1.0 / 3.0), laneDist(l1, l2, 2.0 / 3.0)) * NE_CELL;
    float yc = yRel - gR - gHopAmp * hop;
    float inv = 1.0 / gLip;
    h.dMid = (length(vec2(pm, yc)) - gR) * inv;
    h.dOut = (length(vec2(po, yc)) - gR) * inv;
    h.dIn = 1e3;
    if (gFadeIn > 0.01) {
        float pi = min(laneDist(l1, l2, 5.0 / 12.0), laneDist(l1, l2, 7.0 / 12.0)) * NE_CELL;
        float ri = gR * gFadeIn;
        h.dIn = (length(vec2(pi, yRel - ri - gHopAmp * hop)) - ri) * inv;
    }
    float d = min(h.dMid, min(h.dOut, h.dIn));

    // Neighbours may hold arcs this cell cannot see (a turning tile's ends,
    // a hopping neighbour): below the highest any tube reaches, never step
    // further than the cell's border.
    vec2 af = abs(f0);
    float toEdge = (0.5 - max(af.x, af.y)) * NE_CELL * inv;
    float top = 2.0 * gR + gHopAmp;
    h.d = min(d, max(toEdge + 0.03, yRel - top));
    h.mat = 1.0;
    return h;
}

TruchetHit truchetMap(vec3 p) {
    float yF = p.y + gHalf;
    float yC = gHalf - p.y;
    bool lower = p.y < 0.0;
    TruchetHit h = tubing(p.xz, lower ? yF : yC, lower ? 0.0 : 1000.0);
    float dT = h.d;
    // The far plane's tubing lies wholly below its slab top.
    float top = 2.0 * gR + gHopAmp;
    dT = min(dT, (lower ? yC : yF) - top);
    float dP = min(yF, yC);
    float d = min(dT, dP);
    h.mat = dP < dT ? 0.0 : (h.dMid <= h.dOut && h.dMid <= h.dIn ? 1.0 : (h.dOut <= h.dIn ? 2.0 : 3.0));
    h.d = max(d, NE_BUBBLE - length(p - gEye));
    return h;
}

//#include lib_truchet_field

void main() {
    vec2 uv = view();
    truchetSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term repeats within 64 world units (see the header), so this
    // shift is exact and invisible; without it a highp z far down the 8192
    // travel keeps too little fraction for the tubes' thin halos.
    ro.z = mod(ro.z, NE_ZWRAP);
    gEye = ro;

    float e = truchetEnergy();
    float morph = gTruchetIn.detail;
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float mids = clamp(sndShaped(SND_MIDDLE), 0.0, 1.2) * e;
    float bass = clamp(SND_BASS_PRESENCE, 0.0, 1.5);

    gT = truchetTime();
    gHits = floor(SND_ONSET_COUNT);
    gEnv = SND_ONSET_ENV;
    gHalf = NE_HALF + 1.2 * (gTruchetIn.space - 0.5);
    // TEXTURE the tubes' girth (the shell's texture input, a quarter either way); the bass swells them on top.
    gR = truchetAt(gTruchetIn.texture, NE_R, 0.25) * (1.0 + 0.35 * bass * e) * (1.0 - 0.25 * morph);
    gHopAmp = clamp(0.35 * e * (1.0 + 2.0 * (gTruchetIn.pulse - 0.5)), 0.0, 0.8);
    gFadeIn = smoothstep(0.15, 0.85, morph);
    float wa = 0.8 * (2.0 * gTruchetIn.warp);
    float wb = 0.18 * (2.0 * gTruchetIn.chaos);
    gLip = 1.0 + max(0.393 * wa + 0.785 * wb, 0.29 * wa + 1.07 * wb);

    // Halo falloff per world unit: the mids widen it, FLOW widens it more.
    float kG = (7.0 - 3.0 * mids) * (1.0 - 0.6 * (gTruchetIn.flow - 0.5));

    // ---- primary march, gathering the tubes' halo on the way --------------
    float t = 0.05;
    float hitT = -1.0;
    float hitMat = 0.0;
    vec3 glow = vec3(0.0);  // mid, outer, inner families
    float fs = 0.0;
    float ws = 0.0;
    for (int i = 0; i < NE_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        TruchetHit at = truchetField(p);
        float d = at.d;
        float w = clamp(d, 0.02, 0.5) * exp(-t * NE_FOG);
        vec3 h = vec3(exp(-kG * max(at.dMid, 0.0)), exp(-kG * max(at.dOut, 0.0)), exp(-kG * max(at.dIn, 0.0))) * w;
        glow += h;
        float hs = h.x + h.y + h.z;
        fs += at.fld * hs;
        ws += hs;
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            hitMat = at.mat;
            break;
        }
        t += d * 0.9;
        if (t > NE_FAR) break;
    }

    float hue = 0.05 * sin(gT * 0.017) + 0.12 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue)
              + 0.25 * (gTruchetIn.tone - 0.5);
    float fAvg = 0.12 * fs / max(ws, 1e-4);
    vec3 cMid = neonize(pal(hue + 0.15 + fAvg));
    vec3 cOut = neonize(pal(hue + 0.62 + fAvg));
    vec3 cIn = neonize(pal(hue + 0.38 + fAvg));
    vec3 haze = mix(cMid, cOut, 0.5) * (0.018 + 0.03 * (gTruchetIn.build - 0.5));

    vec3 col = haze;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 surf;
        if (hitMat > 0.5) {
            // A tube: emissive glass, white-hot where it faces the eye.
            vec3 n = truchetNormal(p, max(0.0015, 0.001 * hitT));
            TruchetHit hit = truchetField(p);
            float fl = 0.12 * hit.fld;
            vec3 c = hitMat < 1.5 ? neonize(pal(hue + 0.15 + fl))
                   : (hitMat < 2.5 ? neonize(pal(hue + 0.62 + fl)) : neonize(pal(hue + 0.38 + fl)));
            float facing = clamp(dot(n, -rd), 0.0, 1.0);
            surf = c * (0.30 + 0.45 * facing) + mix(c, vec3(1.0), 0.65) * pow(facing, 3.0) * 0.85;
        } else {
            // Wet black glass: a short second march gathers what it mirrors.
            vec3 n = vec3(0.0, p.y < 0.0 ? 1.0 : -1.0, 0.0);
            vec3 rr = reflect(rd, n);
            vec3 ro2 = p + n * 0.02;
            float t2 = 0.03;
            vec3 rg = vec3(0.0);
            vec3 rhit = vec3(0.0);
            float rBudget = uSteps * 0.45;
            for (int j = 0; j < NE_REFL_STEPS; j++) {
                if (float(j) >= rBudget) break;
                vec3 q = ro2 + rr * t2;
                TruchetHit rat = truchetField(q);
                float d = rat.d;
                float w = clamp(d, 0.02, 0.5) * exp(-t2 * NE_FOG * 1.5);
                rg += vec3(exp(-kG * max(rat.dMid, 0.0)), exp(-kG * max(rat.dOut, 0.0)), exp(-kG * max(rat.dIn, 0.0))) * w;
                if (d < 0.002 + 0.001 * t2) {
                    if (rat.mat > 0.5) {
                        vec3 c = rat.mat < 1.5 ? cMid : (rat.mat < 2.5 ? cOut : cIn);
                        rhit = mix(c, vec3(1.0), 0.4) * 0.7 * exp(-t2 * NE_FOG * 1.5);
                    }
                    break;
                }
                t2 += d * 0.9;
                if (t2 > NE_REFL_FAR) break;
            }
            float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 4.0);
            float gloss = clamp(1.0 + 1.4 * (gTruchetIn.texture - 0.5), 0.2, 1.8);
            vec3 refl = rhit + (cMid * rg.x + cOut * rg.y + cIn * rg.z * gFadeIn) * 0.6;
            surf = pal(hue + 0.55) * 0.012 + refl * (0.18 + 0.55 * fres) * gloss;
        }
        col = mix(haze, surf, exp(-hitT * NE_FOG));
    }

    // The halo, already fogged step by step.
    col += (cMid * glow.x + cOut * glow.y + cIn * glow.z * gFadeIn) * 0.6;

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
