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
//#include lib_sdf3
//#include lib_neon

// Outrun Drive: a night drive down a neon road toward a striped setting sun.
// Left of the road a mountain range rises off the shoulder; right of it a
// skyline stands on its lots; far ranges sit on the horizon and edge lights
// stream past. Flown with camFly; the road runs down +z.
//
// WHAT IS DRAWN, built the way the Flame, Opal and Hollows families are. The
// range is a ridged multifractal four octaves deep, each octave's ridges
// standing on the crests of the one above. Every tower is crowned by setbacks
// folded into quarters three times over, so each roof carries four spires,
// each of those four more and each of those four more: 84 spires a tower. The
// two are one distance field, sphere-traced and shaded once; the flat floor
// under and between them is an analytic plane, so a grazing ray down the road
// ends on it exactly instead of creeping up on it. The ground is ruled by the neon grid, a glowing line at
// every second unit, antialiased by the pixel's footprint; the towers' edges
// glow and their windows are lit. A haze whose colour is the palette's far
// end closes the distance, and the sky behind holds the striped sun, the far
// ranges' rims and the stars.
//
// COST. One loop of OR_STEPS steps (80 at the default Detail, 100 at most),
// each one map(): four octaves of value noise only inside the range's box,
// one lot hash and four boxes for the skyline. The hit adds four map() for
// its normal. The palette is read five times per pixel (orSetup) and mixed.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gOrForm    the towers' crowns grow taller out of their roofs
//   Warp        gOrFlow    orSway(): the road and everything beside it wind
//                          from side to side along the way
//   Turbulence  gOrChaos   the range roughens: each finer octave weighs more
//   Intensity   neonGlow() the emission
//   k0 PULSE the emission, k1 TEXTURE the grid's and the edges' line width,
//   k2 FLOW the bend, k3 TONE the palette position, k4 BUILD the neon's glow,
//   k5 SPACE the reach.
//
// THE SOUND, heard the way the families hear it: lib_sound's shaped reads, in
// orSetup(), on the motion only, never on a colour, a brightness or a beat.
//
//   gOrSwell  the low band, sndShaped(SND_LOW), deepens the Warp's winding,
//   never past the Warp's own reach.
//
// It is times SND_DRIVE, the music's absolute magnitude: how MUCH moves
// follows how loud and dense the music is, and the band only shapes the push.
// No sound band banks the view (owner 2026-11-12, no style spins to the music).
// What moves on a hit is the reaction matrix, through the params above. uTime
// stops in silence and SND_DRIVE falls to 0, so everything here comes to rest.
//
// Every clock phase is whole cycles per uTime wrap (OR_RATE). Every z term has
// a period dividing 1024 (the range's octaves, the lots, the winding, the
// lights, the dashes and the grid), so the eye is rebased into one 1024 tile
// and the rig's 8192 wrap is invisible.
//
// WHY THE ESTIMATE IS SAFE. A lot measures only its own tower, and no tower
// comes within OR_GAP of its lot's edge, so below the tallest roof the
// distance is clamped to the lot edge plus OR_GAP. A crown is a mirror fold
// (abs) of the roof whose copy lies wholly inside its own quadrant, so the
// folded distance is exact. A heightfield's vertical clearance is not a
// distance, so the range's is taken at 0.45, and the step is the largest of
// that, the horizontal distance to the range and the height above its tallest
// peak; a step that lands under a ridge is pulled back to where its two
// samples cross. The winding shears x by at most 3 * 2pi / 64 = 0.3 per unit
// of z, so every distance measured in the wound frame is over by at most 5 %,
// and the march takes 0.85 of each. A sphere round the eye is carved out of
// everything, and the eye is lifted clear of the range.

#define OR_TAU 6.2831853
#define OR_MAX_STEPS 100
/** uSteps (64..128 by Detail, 102 at its default) on this loop, as the families take it. */
#define OR_STEPS (min(uSteps * 0.78, float(OR_MAX_STEPS)))
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define OR_RATE(k) (OR_TAU * float(k) / 7100.0)
#define OR_FLOOR -2.2
#define OR_ROAD 2.4
#define OR_ZWRAP 1024.0
/** One wind of the road, world units along z. */
#define OR_WIND 64.0
/** The range starts this far left of the road's centre line and is fully up 12 units further. */
#define OR_SHOULDER 4.5
#define OR_OCTAVES 4
/** The range's tallest peak above the floor, and the tallest trunk. */
#define OR_LAND_H 5.0
#define OR_TOWER_H 7.0
/** Tower lots, world units: 128 of them along z in one 1024 tile. */
#define OR_LOT 8.0
#define OR_LOTS_Z 128.0
/** Widest trunk half-width; the rest of the lot's 4-unit half is street. */
#define OR_BMAX 2.6
/** How far every tower stays inside its lot's edge: 4 - OR_BMAX, less a margin. */
#define OR_GAP 1.3
#define OR_BUBBLE 0.6

float gOrForm;
float gOrFlow;
float gOrChaos;
/** The low band's deepening of the winding, 0..1.5. */
float gOrSwell;
/** How far the trace reaches, world units. */
float gOrFar;
/** The lines' width and the neon's glow, both 1 at rest. */
float gOrLine;
float gOrGlow;
float gOrTone;
vec3 gOrPal0;
vec3 gOrPal1;
vec3 gOrPal2;
vec3 gOrPal3;
vec3 gOrPal4;

/** How far the road winds to either side, the range's octave gain, how far the crowns grow, and the top of every roof. */
float gWind;
float gGain;
float gGrow;
float gHiMax;
/** The eye, z rebased into 0..1024. */
vec3 gEye;
/** Set by map(): 0 the range, 1 a tower, and the land's vertical clearance (the crossing test). */
float gMat;
float gClear;
/** Set by tower(): the nearest block's local point and half size (its edges and windows), and the tower's hash. */
vec3 gBoxQ;
vec3 gBoxH;
float gTowerId;

/** The user's palette at t, from the five reads of orSetup(): there and back, so no seam. */
vec3 orCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gOrPal0, gOrPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gOrPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gOrPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gOrPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

void orSetup() {
    gOrForm = neonForm();
    gOrFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gOrChaos = neonChaos();
    gOrFar = 80.0 * (1.0 + 0.4 * neonKnob(5));
    gOrLine = 1.0 + neonKnob(1);
    gOrGlow = 1.0 + neonKnob(4);
    gOrTone = 0.6 * neonKnob(3);
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gOrSwell = sndShaped(SND_LOW) * drive;
    gOrPal0 = pal(0.0);
    gOrPal1 = pal(0.25);
    gOrPal2 = pal(0.5);
    gOrPal3 = pal(0.75);
    gOrPal4 = pal(1.0);

    gWind = 3.0 * min(gOrFlow + 0.3 * gOrSwell, 1.0);
    gGain = 0.42 + 0.18 * gOrChaos;
    gGrow = 0.25 + gOrForm;
    // A trunk is at most OR_TOWER_H; its crowns add 2.4 tiers of 0.25 * H * gGrow.
    gHiMax = OR_FLOOR + OR_TOWER_H * (1.0 + 0.6 * gGrow);
}

/** Warp on space: where the road's centre line lies at z. Whole cycles per wind and per uTime wrap. */
float orSway(float z) {
    return gWind * sin(z * (OR_TAU / OR_WIND) + uTime * OR_RATE(110));
}

/** Value noise on a lattice that wraps along y every `period` cells (a whole number), 0..1. */
float wrapNoise(vec2 p, float period) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float z0 = mod(i.y, period);
    float z1 = mod(i.y + 1.0, period);
    float a = motionHash(vec2(i.x, z0));
    float b = motionHash(vec2(i.x + 1.0, z0));
    float c = motionHash(vec2(i.x, z1));
    float d = motionHash(vec2(i.x + 1.0, z1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

/**
 * The range's height above the floor at q (the road's frame: x from its
 * centre line): nothing on and right of the shoulder, a ridged multifractal to
 * its left. The first octave has 64 cells along z per 1024 world units and
 * each next one twice as many, so every octave tiles the rebase.
 */
float land(vec2 q) {
    float side = smoothstep(OR_SHOULDER, OR_SHOULDER + 12.0, -q.x);
    if (side <= 0.0) return 0.0;
    vec2 p = q * 0.0625;
    float period = 64.0;
    float sum = 0.0;
    float amp = 0.5;
    float norm = 0.0;
    float prev = 1.0;
    for (int k = 0; k < OR_OCTAVES; k++) {
        float n = wrapNoise(p, period);
        float r = 1.0 - abs(2.0 * n - 1.0);
        r *= r;
        // Each octave's ridges stand on the crests of the one above: a ridged multifractal.
        sum += amp * r * (0.35 + 0.65 * prev);
        norm += amp;
        prev = r;
        p = p * 2.0 + vec2(17.31, 0.0);
        period *= 2.0;
        amp *= gGain;
    }
    return side * OR_LAND_H * sum / norm;
}

/** A lower bound on the distance to the range at q (the floor is not part of it: orTrace() hits that analytically). */
float mountains(vec3 q) {
    float h = q.y - OR_FLOOR;
    gClear = h;
    // Bounds that need no noise: the range lies left of its shoulder and under its tallest peak.
    float outside = max(q.x + OR_SHOULDER, h - OR_LAND_H);
    // The land is never below the floor, so 0.45 * h already bounds the clearance term from above.
    if (outside >= 0.45 * h) return outside;
    gClear = h - land(q.xz);
    return max(outside, 0.45 * gClear);
}

/**
 * One tower in its lot's frame (q.y up from the floor): a trunk of half-width
 * b and height H, then three crowns. Each folds the roof into four quadrants
 * and stands one narrower block in each, `tier` high (less at each level), so
 * the third crown is 64 spires.
 */
float tower(vec3 q, float H, float b, float tier) {
    // The trunk runs half a unit under the floor, so there is no seam at its foot.
    vec3 bq = q - vec3(0.0, 0.5 * H - 0.5, 0.0);
    vec3 bh = vec3(b, 0.5 * H + 0.5, b);
    float d = sdBox(bq, bh);
    gBoxQ = bq;
    gBoxH = bh;
    vec3 p = q;
    float s = b;
    float base = H;
    for (int k = 0; k < 3; k++) {
        // The quadrant's block sits half the span from each mirror and is 0.8 of half of it wide,
        // so it never reaches the mirror: the fold is exact.
        p.xz = abs(p.xz) - 0.5 * s;
        s *= 0.5;
        float hk = tier * (1.0 - 0.2 * float(k));
        vec3 cq = p - vec3(0.0, base + 0.5 * hk - 0.05, 0.0);
        vec3 ch = vec3(0.8 * s, 0.5 * hk + 0.05, 0.8 * s);
        float dk = sdBox(cq, ch);
        if (dk < d) {
            d = dk;
            gBoxQ = cq;
            gBoxH = ch;
        }
        base += hk;
    }
    return d;
}

/** The skyline at q (the road's frame): one tower, or an empty lot, per lot right of the road. */
float skyline(vec3 q) {
    vec2 lot = floor(q.xz / OR_LOT);
    vec2 f = q.xz - (lot + 0.5) * OR_LOT;
    // Every other tower is at least this far: past this lot's edge and OR_GAP more, or below the roofs.
    float far = max(0.5 * OR_LOT - max(abs(f.x), abs(f.y)) + OR_GAP, q.y - gHiMax);
    // The road's own column and everything left of it hold no towers.
    if (lot.x < 0.5) return far;
    uint u = uhash3(uvec3(ivec3(int(lot.x), 11, int(mod(lot.y, OR_LOTS_Z)))));
    float h = uhashUnit(u);
    // An empty lot now and then, so the skyline breaks.
    if (h < 0.15) return far;
    uint u2 = uhash(u);
    float H = OR_TOWER_H * (0.35 + 0.65 * uhashUnit(u2));
    float b = OR_BMAX * (0.6 + 0.4 * uhashUnit(uhash(u2)));
    float d = tower(vec3(f.x, q.y - OR_FLOOR, f.y), H, b, H * 0.25 * gGrow);
    gTowerId = h;
    return min(d, far);
}

float map(vec3 p) {
    vec3 q = vec3(p.x - orSway(p.z), p.y, p.z);
    float d = mountains(q);
    gMat = 0.0;
    float ds = skyline(q);
    if (ds < d) {
        d = ds;
        gMat = 1.0;
    }
    return max(d, OR_BUBBLE - length(p - gEye));
}

/** The field's gradient at p, unit length; straight back along the ray where the field is flat. */
vec3 orNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec3 g = e.xyy * map(p + e.xyy) + e.yyx * map(p + e.yyx) + e.yxy * map(p + e.yxy) + e.xxx * map(p + e.xxx);
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

float orDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** 1D value noise, piecewise linear (jagged peaks), wrapping every `period` cells. */
float ridge1(float x, float period, float seed) {
    float i = floor(x);
    float a = motionHash(vec2(mod(i, period), seed));
    float b = motionHash(vec2(mod(i + 1.0, period), seed));
    return mix(a, b, fract(x));
}

/** A far range's top elevation (tan of the angle) at azimuth `az`. */
float rangeTop(float az, float seed) {
    float x = az / OR_TAU * 20.0;
    float m = ridge1(x, 20.0, seed);
    return 0.012 + 0.11 * m * m + 0.035 * ridge1(x * 3.0, 60.0, seed + 11.0);
}

/** The night sky: the gradient, the stars, the striped sun and the far ranges' rims. */
vec3 orSky(vec3 rd, vec3 horizon) {
    float hz = length(rd.xz);
    float e = rd.y / max(hz, 1.0e-3);
    // atan(0, 0) is undefined (NaN on some drivers) looking straight up.
    float az = hz > 1.0e-4 ? atan(rd.x, rd.z) : 0.0;
    vec3 col = mix(horizon, orCol(gOrTone + 0.8) * 0.02, smoothstep(0.0, 0.6, e));

    vec3 sc = rd * 150.0;
    float s = hashCell(ivec3(floor(sc)));
    float star = step(0.993, s) * (1.0 - smoothstep(0.0, 0.35, length(fract(sc) - 0.5)));
    float tw = 0.75 + 0.25 * sin(uTime * OR_RATE(5650) + s * 600.0);
    col += mix(vec3(1.0), orCol(s), 0.3) * 0.75 * star * tw * smoothstep(0.08, 0.3, e);

    // The sun: stripes across its lower half, the gaps widening downward and the stripes sliding down.
    vec2 sp = vec2(az, e - 0.12) / 0.26;
    float r = length(sp);
    float disc = 1.0 - smoothstep(0.97, 1.0, r);
    vec3 sunCol = mix(orCol(gOrTone + 0.9), orCol(gOrTone + 0.65), smoothstep(-1.0, 1.0, sp.y)) * 1.4;
    float band = fract(sp.y * 5.0 + uTime * OR_RATE(852) / OR_TAU);
    float gap = 0.5 * (1.0 - smoothstep(-1.0, 0.25, sp.y));
    float cut = step(sp.y, 0.25) * (1.0 - smoothstep(gap - 0.04, gap, band));
    col = mix(col, sunCol, disc * (1.0 - cut));
    col += orCol(gOrTone + 0.9) * 0.3 * exp(-max(r - 1.0, 0.0) * 2.5) * (1.0 - disc);

    // Two far ranges, their rims lit by the neon's glow.
    float hb = rangeTop(az, 61.0) + 0.02;
    float hf = 0.6 * rangeTop(az + 1.3, 66.0);
    if (e < hb) col = mix(orCol(gOrTone + 0.65) * 0.05, col, 0.15);
    col += orCol(gOrTone + 0.6) * exp(-abs(e - hb) * 220.0) * 0.5 * gOrGlow;
    if (e < hf) col = orCol(gOrTone + 0.5) * 0.015;
    col += orCol(gOrTone) * exp(-abs(e - hf) * 260.0) * 0.7 * gOrGlow;
    return col + horizon * 0.6 * exp(-max(e, 0.0) * 25.0);
}

/** The light along one ray: the range and the towers sphere-traced, the floor hit analytically, then shaded once. */
vec3 orTrace(vec3 ro, vec3 rd) {
    float n = OR_STEPS;
    vec3 horizon = orCol(gOrTone + 0.5) * 0.32;
    float tFloor = rd.y < -1.0e-4 ? (ro.y - OR_FLOOR) / -rd.y : 1.0e4;
    float tEnd = min(tFloor, gOrFar);
    float top = max(gHiMax, OR_FLOOR + OR_LAND_H) + 0.1;
    float t = 0.05 + 0.1 * orDither();
    float tPrev = t;
    float cPrev = ro.y - OR_FLOOR;
    float hitT = -1.0;
    float mat = 0.0;
    for (int i = 0; i < OR_MAX_STEPS; i++) {
        if (float(i) >= n || t > tEnd) break;
        vec3 p = ro + rd * t;
        // Above every peak and roof and climbing: nothing left to hit.
        if (rd.y >= 0.0 && p.y > top) break;
        float d = map(p);
        float eps = 0.0015 * (1.0 + t);
        if (d < eps) {
            hitT = t;
            mat = gMat;
            // A step that landed under a ridge: back to where the two samples cross.
            if (gMat < 0.5 && gClear < 0.0) hitT = mix(tPrev, t, clamp(cPrev / max(cPrev - gClear, 1.0e-4), 0.0, 1.0));
            break;
        }
        tPrev = t;
        cPrev = gClear;
        t += max(d * 0.85, 0.25 * eps);
    }
    bool onFloor = false;
    if (hitT < 0.0 && tFloor < gOrFar) {
        hitT = tFloor;
        mat = 0.0;
        onFloor = true;
    }
    if (hitT < 0.0) return rd.y < 0.0 ? horizon : orSky(rd, horizon);

    vec3 p = ro + rd * hitT;
    vec3 nor = vec3(0.0, 1.0, 0.0);
    if (!onFloor) {
        nor = orNormal(p, rd, 0.002 * (1.0 + hitT));
        // Restore the hit's globals after the normal's taps.
        map(p);
    }
    float sway = orSway(p.z);
    float qx = p.x - sway;
    float ndv = clamp(dot(nor, -rd), 0.0, 1.0);
    float rim = (1.0 - ndv) * (1.0 - ndv) * (1.0 - ndv);
    // The sun sits low straight ahead.
    float dif = max(dot(nor, normalize(vec3(0.0, 0.12, 1.0))), 0.0);
    float depth = hitT / gOrFar;
    // How wide one pixel is on the ground here, world units.
    float px = hitT * 2.0 / (max(uCamFocal, 0.2) * uResolution.y * max(ndv, 0.08));
    vec3 col;
    if (mat < 0.5) {
        // ---- the floor and the range: the neon grid over dark ground ----
        vec2 g = abs(fract(vec2(qx, p.z) * 0.5 + 0.5) - 0.5) * 2.0;
        float w = 0.025 * gOrLine;
        float line = 1.0 - smoothstep(w, w + 1.5 * px, min(g.x, g.y));
        // Where a cell shrinks under a few pixels the lines would moire: settle on their average.
        line = mix(line, 0.2, smoothstep(0.3, 0.9, px));
        float slope = clamp(1.0 - nor.y, 0.0, 1.0);
        col = orCol(gOrTone + 0.55) * (0.015 + (0.03 + 0.08 * dif) * slope) + orCol(gOrTone + 0.6) * rim * 0.3 * slope;
        col += orCol(gOrTone + 0.15 * depth) * line * 0.9 * gOrGlow;

        float ax = abs(qx);
        if (ax < OR_ROAD + 0.6) {
            float road = 1.0 - smoothstep(OR_ROAD - 0.05, OR_ROAD + 0.05, ax);
            col *= 1.0 - 0.6 * road;
            float edge = 1.0 - smoothstep(0.04 * gOrLine, 0.12 * gOrLine, abs(ax - (OR_ROAD - 0.15)));
            float dash = step(0.5, fract(p.z * 0.25)) * (1.0 - smoothstep(0.03 * gOrLine, 0.1 * gOrLine, ax));
            col += orCol(gOrTone + 0.5) * (edge * 0.9 + dash * 0.6) * gOrGlow;
        }
        // Edge lights, one every 4 units each side, streaming toward the eye at 6 units a second.
        float lz = (fract((p.z + uTime * OR_RATE(10650) * (4.0 / OR_TAU)) * 0.25) - 0.5) * 4.0;
        float dz = max(abs(lz) - 0.12, 0.0);
        float dx = ax - (OR_ROAD + 0.4);
        float glow = exp(-(dx * dx + dz * dz) * 16.0);
        col += (orCol(gOrTone + (qx > 0.0 ? 0.35 : 0.15)) * 1.2 + 0.2) * glow;
    } else {
        // ---- a tower: dark glass, neon on every edge, lit windows ----
        // An edge is where two of the block's faces are both near.
        vec3 e = gBoxH - abs(gBoxQ);
        float e1 = min(e.x, min(e.y, e.z));
        float e3 = max(e.x, max(e.y, e.z));
        float e2 = e.x + e.y + e.z - e1 - e3;
        float ew = 0.03 * gOrLine;
        float edge = 1.0 - smoothstep(ew, ew + 1.5 * px, e2);
        float hue = gOrTone + 0.5 + 0.35 * gTowerId;
        col = orCol(hue + 0.3) * (0.02 + 0.05 * dif) + orCol(hue) * rim * 0.3;

        // Windows on the walls, a grid in the block's own frame, settling on their average far off.
        float wall = 1.0 - step(0.5, abs(nor.y));
        float along = abs(nor.x) > abs(nor.z) ? gBoxQ.z : gBoxQ.x;
        vec2 wc = vec2(along * 2.4, (p.y - OR_FLOOR) * 1.9);
        vec2 wf = fract(wc);
        float shape = smoothstep(0.18, 0.26, wf.x) * (1.0 - smoothstep(0.74, 0.82, wf.x))
                    * smoothstep(0.25, 0.33, wf.y) * (1.0 - smoothstep(0.77, 0.85, wf.y));
        float wid = hashCell(ivec3(int(floor(wc.x)), int(floor(wc.y)), int(gTowerId * 4096.0)));
        float lit = step(wid, 0.3);
        float avg = smoothstep(0.08, 0.3, px);
        // A slow drift of the lit rows keeps the windows alive.
        float sweep = 0.6 + 0.4 * sin(uTime * OR_RATE(790) + gTowerId * 40.0 + floor(wc.y) * 0.9);
        col += orCol(hue + 0.12) * mix(lit * shape, 0.075, avg) * sweep * wall;
        col += orCol(gOrTone + 0.2 * gTowerId) * edge * 1.1 * gOrGlow;
    }
    // The haze: the palette's far end closes the distance.
    return mix(col, horizon, 1.0 - exp(-3.0 * depth));
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 orFinish(vec3 col, float exposure) {
    col *= exposure * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    col = grade(neonAces(col));
    // No pixel leaves as a NaN or an infinity: the feedback pass would carry it for good.
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    return vec4(col, 1.0);
}

void main() {
    // Life's births (lib_grow): no field loop here to run them in, so each is the picture's middle again,
    // small, at its spawn point; uv itself at Life 0.
    vec2 uv = growBirths2(view());
    orSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, OR_ZWRAP);
    ro.y = max(ro.y, OR_FLOOR + land(vec2(ro.x - orSway(ro.z), ro.z)) + 0.45);
    gEye = ro;
    fragColor = orFinish(orTrace(ro, rd), 1.3);
}
