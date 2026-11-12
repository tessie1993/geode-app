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

// Arbor Bolt: flying low down a storm valley at night, a churning cloud deck
// overhead, rain slanting past, and lightning forking down out of the clouds
// ahead onto the ridges, lighting the wet rock and the cloud base around it.
//
// The bolt is the same tree the flat style drew: arborTree2 grown downward with
// a LOPSIDED fork (the +x child the long, barely turned main channel, the -x
// child the short side spark flung wide) and a large per-branch jitter hashed
// from the branch id, so every strike kinks somewhere new. In 3D it stands in a
// vertical plane hung from the cloud base, turned by a hash, some way ahead of
// the eye. Each pixel meets that plane analytically (one ray-plane divide) and
// reads the tree there, so the bolt costs what it cost in 2D and stays sharp at
// any distance. The terrain occludes it (the bolt is only drawn in front of the
// ray's terrain hit), the cloud deck hides its top, and it is a light: the
// ground, the wet specular and the cloud base all take its colour with distance.
//
// Flash safety: as before, the bolt's own brightness never moves with a hit. A
// new bolt GROWS in down the sky while the old one fades out on the same
// envelope, so the light in the frame, the terrain's and the clouds' included,
// is handed over, not added.
//
// ---- camera ------------------------------------------------------------------
//
// camFly down the valley (+z). The eye's z is rebased by BO_ZWRAP (256); the
// terrain noise (64 and 128 cells over 256 units) and the cloud noise (16, 32
// and 64 cells over 256 units) wrap their z lattice on exactly that period, so
// neither the rebase nor the rig's 8192 travel wrap ever shows. The bolts are
// placed relative to the eye along +z (and at a hashed world x across the
// valley), so a strike stays ahead of the flight, and the rain is drawn on
// world-fixed directions, so it turns with the view without a seam.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets STRIKE. SND_ONSET_COUNT is the bolt's seed: every onset is a new strike,
// with its own channel, kinks, side sparks, place across the valley and turn.
// The onset envelope (SND_ONSET_ENV, 0.22 s) is the strike's progress, the new
// bolt racing down from the cloud while the last one fades. Bass
// (SND_BASS_PRESENCE) churns the clouds (the depth of their domain warp, never
// their clock), air (sndSpectrum(0.93)) thickens the rain. Brightness follows
// SND_LEVEL only.
//
// ---- macros and knobs ---------------------------------------------------------
//
// Speed: uTime drives the clouds, rain and the bolt's crawl. Complexity:
// uMorph adds a fork level to every strike (arbLevels, which also drops twig
// levels on a small Detail budget). Chaos: uTurbulence and uWarp widen the
// per-branch kink jitter into a wilder, more ragged bolt. Twist (uTwist) leans
// every fork the same way, so the channel winds into a spiral. Colour: the halo,
// the cloud light, the rock and the sky from pal(); the core stays near white.
// Energy: uIntensity scales the cloud churn and rain. Space: the rig.
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE how far the
// bolt's light reaches over the land, k1 TEXTURE the rain, k2 FLOW the cloud
// churn, k3 TONE the hue, k4 BUILD the bolt's halo, k5 SPACE how far ahead the
// strikes land. The mids lean the bolt's forks (arbMidBow).
//
// ---- why the distance estimate is safe ----------------------------------------
//
// Only the terrain is marched. It is a heightfield h(x, z), so p.y - h divided
// by sqrt(1 + L^2), L a bound on |grad h|, is a lower bound on the distance.
// L: the valley walls rise BO_RISE over BO_BANK on a smoothstep (slope at most
// 1.5 * 5 / 9 = 0.83); a value noise with a smoothstep fade has |grad| at most
// 1.5 * sqrt(2) per cell, so the broad noise (amplitude 2, 0.25 cells a unit)
// adds 1.06 and its wall weighting 0.22, the ridged fine noise (its fold
// doubles the slope; 0.45 at 0.5 cells a unit) 0.95. L < 3.07, and
// sqrt(1 + 3.07^2) < BO_LIP. The bolt and the clouds are not marched (both are
// exact ray-plane hits), so they add nothing to it. A sphere around the eye is
// carved out of the terrain, so the eye is never inside a ridge, and rays that
// climb above the highest ridge stop at once.

#define BO_MAX_STEPS 96
#define BO_FAR 60.0
#define BO_TAU 6.2831853
/** Rebase period of the eye's z: every z lattice wraps on it, and it divides 8192. */
#define BO_ZWRAP 256.0
#define BO_FLOOR -3.0
#define BO_RISE 5.0
/** Half-width of the open valley floor before the walls climb. */
#define BO_WIDTH 5.0
#define BO_BANK 9.0
/** Above every ridge: floor + rise + both noise amplitudes. */
#define BO_HMAX 4.5
/** The cloud deck's base. */
#define BO_CLOUD 6.0
/** Gradient bound of the heightfield (see the header). */
#define BO_LIP 3.25
#define BO_BUBBLE 0.8
/** World units per unit of the flat bolt's tree: its main channel (~2.1) spans cloud to ground. */
#define BO_SCALE 4.6
/** How far across the valley a strike may land, world units. */
#define BO_SPREAD 18.0

vec3 gEye;
/** The flat bolt's form at rest, mapped by the shell (each strike adds its own lean and jitter). */
ArborForm2 gBolt;

/** Value noise on (x, z) whose z lattice wraps every `per` cells. */
float boNoise2(vec2 p, float per) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float z0 = mod(i.y, per);
    float z1 = mod(i.y + 1.0, per);
    float a = hash13(vec3(i.x, z0, 5.0));
    float b = hash13(vec3(i.x + 1.0, z0, 5.0));
    float c = hash13(vec3(i.x, z1, 5.0));
    float d = hash13(vec3(i.x + 1.0, z1, 5.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

/** Value noise in 3D whose z lattice wraps every `per` cells (y is free: the cloud's clock). */
float boNoise3(vec3 p, float per) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float z0 = mod(i.z, per);
    float z1 = mod(i.z + 1.0, per);
    float a = mix(hash13(vec3(i.x, i.y, z0)), hash13(vec3(i.x + 1.0, i.y, z0)), u.x);
    float b = mix(hash13(vec3(i.x, i.y + 1.0, z0)), hash13(vec3(i.x + 1.0, i.y + 1.0, z0)), u.x);
    float c = mix(hash13(vec3(i.x, i.y, z1)), hash13(vec3(i.x + 1.0, i.y, z1)), u.x);
    float d = mix(hash13(vec3(i.x, i.y + 1.0, z1)), hash13(vec3(i.x + 1.0, i.y + 1.0, z1)), u.x);
    return mix(mix(a, b, u.y), mix(c, d, u.y), u.z);
}

/** fbm of boNoise3: each octave doubles the frequency and the wrap, so the period holds. */
float boCloudFbm(vec3 p, float per, int octaves) {
    float sum = 0.0;
    float amp = 0.5;
    float norm = 0.0;
    for (int i = 0; i < 3; i++) {
        if (i >= octaves) break;
        sum += amp * boNoise3(p, per);
        norm += amp;
        // Offset on x and the clock only: a z offset would have to be whole cells.
        p = p * 2.0 + vec3(37.13, 11.71, 0.0);
        per *= 2.0;
        amp *= 0.5;
    }
    return sum / norm;
}

float terrainH(vec2 xz) {
    float wall = smoothstep(BO_WIDTH, BO_WIDTH + BO_BANK, abs(xz.x));
    // 64 cells over 256 units and 128 over 256: both wrap on BO_ZWRAP.
    float n1 = boNoise2(xz * 0.25, 64.0);
    float n2 = 1.0 - abs(2.0 * boNoise2(xz * 0.5 + vec2(31.7, 0.0), 128.0) - 1.0);
    return BO_FLOOR + BO_RISE * wall + 2.0 * n1 * (0.35 + 0.65 * wall) + 0.45 * n2;
}

/** The world the march meets: the valley, carved round the eye. The strikes are flat trees met analytically (boltHit). */
ArborHit arborMap(vec3 p) {
    float d = (p.y - terrainH(p.xz)) / BO_LIP;
    return arbHit(max(d, BO_BUBBLE - length(p - gEye)));
}

//#include lib_arbor_field

vec3 terrainNormal(vec2 xz, float e) {
    float hx = terrainH(xz - vec2(e, 0.0)) - terrainH(xz + vec2(e, 0.0));
    float hz = terrainH(xz - vec2(0.0, e)) - terrainH(xz + vec2(0.0, e));
    return normalize(vec3(hx, 2.0 * e, hz));
}

/**
 * Strike `n` in its vertical plane: where the ray meets the plane (tB, negative
 * when behind), the tree's distance there in the flat bolt's units (dB), its
 * pixel footprint in those units (px), a 0..1 mask (in front, not edge-on, below
 * the cloud base) and the strike's light position (lp). Called unconditionally,
 * so the fwidth() inside sits in uniform control flow.
 */
void boltHit(float n, vec3 ro, vec3 rd, float grow, float chaos, float t, float ahead,
             out float tB, out float dB, out float px, out float mB, out vec3 lp) {
    float yaw = (hash11(n * 3.1 + 0.7) - 0.5) * 0.9;
    vec3 nrm = vec3(sin(yaw), 0.0, cos(yaw));
    vec3 ax = vec3(cos(yaw), 0.0, -sin(yaw));
    vec3 c = vec3((hash11(n * 1.7 + 0.3) - 0.5) * BO_SPREAD, BO_CLOUD, ro.z + ahead + 8.0 * hash11(n * 5.3 + 0.1));
    float den = dot(rd, nrm);
    float sden = (den < 0.0 ? -1.0 : 1.0) * max(abs(den), 1e-3);
    tB = dot(c - ro, nrm) / sden;
    vec3 h = ro + rd * tB;
    vec2 q = vec2(dot(h - c, ax), h.y - c.y) / BO_SCALE;
    // The flat style's frame: the tree grows down from the cloud.
    vec2 p = -q;
    px = max(length(fwidth(p)), 1e-4);
    float lean = 0.03 * sin(t * 1.7 + n) + (hash11(n * 2.9) - 0.5) * 0.12 + arbMidBow();
    ArborForm2 f = gBolt;
    f.lean += lean;
    f.jitter = 0.95 + 0.6 * chaos;
    dB = arborTree2(p, f, grow, n * 13.37).d;
    mB = step(0.0, tB) * smoothstep(0.03, 0.2, abs(den)) * (1.0 - smoothstep(-0.03, 0.01, q.y));
    lp = c - vec3(0.0, 0.55 * (BO_CLOUD - BO_FLOOR), 0.0);
}

/** The bolt's light on a surface at p with normal nrm: wrap diffuse plus a wet specular. */
vec3 boltLight(vec3 p, vec3 nrm, vec3 rd, vec3 lp, vec3 halo, float a) {
    vec3 l = lp - p;
    float dist = length(l);
    l /= max(dist, 1e-3);
    float dif = clamp((dot(nrm, l) + 0.2) / 1.2, 0.0, 1.0);
    float spec = pow(max(dot(reflect(rd, nrm), l), 0.0), 24.0);
    return halo * (0.9 * dif + 0.6 * spec) * a / (1.0 + 0.003 * dist * dist);
}

/** One layer of slanted rain on world-fixed directions; `cols` whole columns round the full turn. */
float rainLayer(vec3 rd, float cols, float slant, float fall, float t, float dens, float seed) {
    float ang = fract(atan(rd.x, rd.z) / BO_TAU);
    float elev = clamp(rd.y / max(length(rd.xz), 1e-3), -3.0, 3.0);
    vec2 rg = vec2(ang * cols + elev * slant, elev * 3.0 + t * fall);
    // The column index wraps with the turn, so the seam behind is just another column.
    float drop = hash13(vec3(mod(floor(rg.x), cols), floor(rg.y), seed));
    return step(1.0 - dens, drop) * (1.0 - smoothstep(0.0, 0.5, abs(fract(rg.x) - 0.5)));
}

void main() {
    vec2 uv = arbView();
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)

    // The normalization shell (lib_arbor): every variable from its driver, mapped to its range here, once.
    ArborIn N = arborIn();
    float chaos = arbSpan(N.chaos, 0.0, 0.0, 2.0);
    float ahead = arbSpan(N.reach, 20.0, 15.0, 25.0);
    float reach = arbSpan(N.emit, 1.0, 0.4, 1.6);
    float churnK = arbSpan(N.flow, 1.0, 0.4, 1.6);
    float buildK = arbSpan(N.glow, 1.0, 0.4, 1.6);
    float densK = arbSpan(N.density, 1.0, 0.3, 1.7);
    float hueShift = arbSpan(N.tone, 0.0, -0.06, 0.06);
    gBolt = arborShape2(ArborForm2(vec2(0.14, 0.80), vec2(0.86, 0.46), 0.0, 0.0, 0.30, 0.026, 0.85), N);

    float energy = uIntensity;
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float bass = clamp(SND_BASS_PRESENCE, 0.0, 1.5) * energy;
    float air = clamp(sndSpectrum(0.93), 0.0, 1.0) * energy;
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);
    float n = floor(SND_ONSET_COUNT);

    float hue = 0.74 + 0.03 * sin(t * 0.04) + hueShift;
    vec3 core = mix(vec3(1.0), pal(hue + 0.1), 0.15);
    vec3 halo = pal(hue + 0.12);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z lattice wraps on BO_ZWRAP (a power of two), so this shift is
    // exact and invisible, and z keeps its precision far down the travel.
    ro.z = mod(ro.z, BO_ZWRAP);
    gEye = ro;

    // ---- the strikes --------------------------------------------------------
    float levels = arbLevels(9.0, 1.0);
    float tN;
    float dN;
    float pxN;
    float mN;
    vec3 lpN;
    float tO;
    float dO;
    float pxO;
    float mO;
    vec3 lpO;
    boltHit(n, ro, rd, 0.5 + levels * (1.0 - onset), chaos, t, ahead, tN, dN, pxN, mN, lpN);
    boltHit(n - 1.0, ro, rd, levels, chaos, t, ahead, tO, dO, pxO, mO, lpO);
    float aNew = 1.0 - 0.6 * onset;
    float aOld = onset;

    // ---- the valley ---------------------------------------------------------
    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < BO_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > BO_FAR) break;
        vec3 p = ro + rd * tm;
        // Climbing above every ridge: nothing left to hit.
        if (p.y > BO_HMAX && rd.y >= 0.0) break;
        used = float(i);
        float d = arborField(p).d;
        float eps = 0.0015 + 0.0012 * tm;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.95, 0.003 * tm);
    }

    // Storm haze: the old sky's dark, plus the air the strikes light.
    vec3 fogCol = pal(hue) * 0.03 + vec3(0.01, 0.008, 0.02);
    float airN = pow(max(dot(rd, normalize(lpN - ro)), 0.0), 14.0) * aNew;
    float airO = pow(max(dot(rd, normalize(lpO - ro)), 0.0), 14.0) * aOld;
    fogCol += halo * (airN + airO) * 0.10 * reach;

    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 nrm = terrainNormal(p.xz, 0.02 + 0.0015 * hitT);
        float ao = arbStepAO(used, uSteps);
        float wallK = smoothstep(BO_WIDTH, BO_WIDTH + BO_BANK, abs(p.x));
        vec3 rock = pal(hue + 0.55) * 0.03 + vec3(0.006, 0.005, 0.012);
        // Dim fill from the cloud deck above.
        vec3 sky = pal(hue + 0.05) * 0.05 * (0.4 + 0.6 * clamp(nrm.y, 0.0, 1.0));
        col = rock * ao + sky * ao * (0.6 + 0.4 * wallK);
        col += (boltLight(p, nrm, rd, lpN, halo, aNew) + boltLight(p, nrm, rd, lpO, halo, aOld)) * 0.5 * reach * (0.5 + 0.5 * ao);
        col = mix(col, fogCol, 1.0 - exp(-hitT * 0.035));
    } else if (rd.y > 0.0) {
        // ---- the cloud deck -------------------------------------------------
        float tc = (BO_CLOUD - ro.y) / max(rd.y, 1e-3);
        vec2 cp = ro.xz + rd.xz * tc;
        cp.x += t * 0.6;
        float churn = (0.6 + 0.8 * bass) * churnK;
        // 1/16 of a cell per unit: 16 cells over 256 units, doubling per octave.
        vec3 c3 = vec3(cp.x / 16.0, t * 0.05, cp.y / 16.0);
        float warp = boCloudFbm(c3 * 2.0 + vec3(5.2, 1.3, 0.0), 32.0, 2);
        float cloud = boCloudFbm(c3 + vec3(warp * churn, 0.0, 0.0), 16.0, 3);
        vec3 cc = mix(pal(hue) * 0.03 + vec3(0.01, 0.008, 0.02), pal(hue + 0.05) * 0.22, smoothstep(0.35, 0.8, cloud));
        // The strikes light the cloud base around where they leave it.
        float litN = exp(-length(cp - lpN.xz) * 0.07) * aNew;
        float litO = exp(-length(cp - lpO.xz) * 0.07) * aOld;
        cc += halo * cloud * (litN + litO) * 0.55 * reach;
        float farK = 1.0 - exp(-tc * 0.02);
        col = mix(cc, fogCol, max(farK, 1.0 - smoothstep(0.0, 0.08, rd.y)));
    }

    // ---- the bolts, in front of whatever the ray found ------------------------
    float sceneT = hitT > 0.0 ? hitT : 1e4;
    float vN = mN * step(tN, sceneT) * aNew;
    float vO = mO * step(tO, sceneT) * aOld;
    float nearN = max(dN, 0.0);
    float nearO = max(dO, 0.0);
    float fogN = exp(-max(tN, 0.0) * 0.012);
    float fogO = exp(-max(tO, 0.0) * 0.012);
    col += halo * (exp(-nearN * 28.0) * 0.35 + exp(-nearN * 7.0) * 0.06) * vN * fogN * buildK;
    col += halo * (exp(-nearO * 28.0) * 0.35 + exp(-nearO * 7.0) * 0.06) * vO * fogO * buildK;
    col = mix(col, core, arbInk(dN, pxN * 0.8) * vN);
    col = mix(col, core, arbInk(dO, pxO * 0.8) * vO);

    // ---- rain ---------------------------------------------------------------
    float dens = 0.06 * (0.3 + air) * densK;
    float rain = rainLayer(rd, 256.0, 12.0, 9.0, t, dens, 2.0) * 0.08
               + rainLayer(rd, 512.0, 16.0, 13.0, t, dens, 3.0) * 0.04;
    // Near a strike the rain catches its light.
    col += pal(hue + 0.05) * rain * (1.0 + 3.0 * (airN + airO) * reach);

    col *= 0.88 + 0.15 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.3, 2.0, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
