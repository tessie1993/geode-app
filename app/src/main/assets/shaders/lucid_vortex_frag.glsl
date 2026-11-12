#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP; uSoundTex is R32F.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_touch
//#include lib_sdf3
//#include lib_lucid

// Lucid Vortex: a maelstrom of molten metal, flown down its throat. The old
// style swirled a plane about a wandering centre, the turn growing as 1/r, and
// advected the swirled plane through a curl field level by level. Now the
// whirlpool is a real bore around the world's z axis: a hot shell whose wall
// is wound into a spiral that tightens with DEPTH the way the old one
// tightened toward its centre (far ahead reads as the tight core, near as the
// broad arcs), rotating as a whole, breathing into throats every 32 units,
// with embers circling the axis in the open air, the inner rings faster than
// the outer ones as in any real vortex.
//
// The wall's paint is still this style's: curl advection (fluidWarp, one call
// per level), so the material shears into filaments and never knots, read as
// heat through a blackbody ramp mixed with the palette's metal. The heat is
// emissive; a dim headlight on the eye adds a cool sheen to the crust so the
// relief reads, and the far end of the bore is the dark drain.
//
// ---- camera ------------------------------------------------------------------
//
// camFly: the dive down the throat is the rig's fly speed, its dolly and
// drop pushes already in uCamPos. The eye's z is rebased by LV_ZWRAP = 32.
// Every z term is wrap-exact: the wall's relief uses u = z * TAU / 16 times
// whole numbers, the standing twist turns once per 32 units, the throat
// breathes with period 32, the paint's z noise is a lattice wrapped every 8
// cells at 8 / 32 cells per unit (each octave doubles both), its curl offset
// circles once per 32 units, and the ember slices are indexed mod 16 slices of
// 2 units. The energy winding and the drain's wander are measured from the eye,
// so the rebase moves both sides alike. 32 divides 8192, so the rig's travel
// wrap never shows either.
//
// ---- audio -------------------------------------------------------------------
//
// Relative energy (uEnergyRel, against the track's own 20 s norm) sets the
// SWIRL TIGHTNESS, as before: a passage louder than the track's norm winds
// the wall's spiral tighter per unit of depth, a quiet one relaxes it into
// long arcs. It is a winding, not a rate, so it never jumps the spin.
// uOrbit (the slow wander that re-targets on novelty and sections) leans the
// drain: the bore's far end swings toward it. A finger drags the eye of the
// vortex: the axis follows the touch anchor. Onsets (SND_ONSET_ENV, on
// lucidBump so they ease in) make the embers flare. SND_ENERGY_TREND sets
// the overall glow gently.
//
// ---- macros and knobs ----------------------------------------------------------
//
// Speed: in uTime (the whirl's rotation, the embers' orbits) and the rig's fly
// speed. Complexity: uMorph past 0.5 adds a curl level and an octave
// (lucidDetail), and uMorph deepens the wall's relief. Chaos:
// uTurbulence/uWarp raise the curl advection (lucidChaos) and jitter the paint
// (lucidJitter); uTwist adds to the winding. Color: the metal is pal(), the
// glow the blackbody ramp. Energy: uIntensity scales the energy winding, the
// drain's wander and the ember flare. Space: uZoom, uRotation and the
// kaleidoscope through view().
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the ember
// flare, k1 TEXTURE the filaments' sharpness, k2 FLOW the curl advection,
// k3 TONE the metal's hue, k4 BUILD the heat, k5 SPACE the bore radius.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// The geometry reads only the cheap relief field (wallField, |f| <= 1), never
// the curl nest, whose gradient has no useful bound; the nest paints the hit
// point only. wallField slopes at most 4.8 per radian of angle and 3.1 per
// unit of u (1.2 per world unit along z). The relief is at most
// LV_RELIEF_MAX = 0.4 and the wall never comes nearer the axis than ~2.75
// units, so across the wall it slopes < 0.7, along z < 0.48. The winding
// (standing twist 0.2 rad per unit plus the energy winding, clamped to 0.3)
// moves the angle along z by < 0.5 rad per unit, adding 0.5 * 4.8 * 0.4 < 0.96;
// the throat's breathing adds 0.1 and the drain's lean (gradient
// 0.08 * |wander| with |wander| <= 1.5) 0.12. sqrt(1 + 0.7^2 + 1.66^2) < 2.06,
// so dividing the radial gap by LV_LIP = 2.2 keeps every step short of the
// wall. Near the axis the angular slope per world unit grows as 1/r, but there
// the radial gap is more than three relief heights, so the estimate is still
// below the true distance. A sphere around the eye is carved out of the shell,
// so the eye is never inside it however far a finger drags the axis.

#define LV_MAX_STEPS 96
#define LV_TAU 6.2831853
/** Rebase period of the eye's z; divides 8192. */
#define LV_ZWRAP 32.0
/** u = z * LV_UF: the relief's depth coordinate, period 16. */
#define LV_UF (6.2831853 / 16.0)
#define LV_RADIUS 4.2
#define LV_SHELL 0.35
#define LV_RELIEF_MAX 0.4
#define LV_LIP 2.2
#define LV_FAR 40.0
#define LV_BUBBLE 0.8
/** The paint's z noise: cells per world unit, and the lattice period that makes 32 units. */
#define LV_ZCELLS 0.25
#define LV_ZPERIOD 8.0

vec3 gEye;
vec2 gAxis;
vec2 gWander;
float gRadius;
float gRelief;
float gWind;
float gSpin;
float gPhase;

vec3 blackbody(float h) {
    h = clamp(h, 0.0, 1.0);
    return clamp(vec3(h * 2.6, h * 2.6 - 0.9, h * 3.2 - 2.2), 0.0, 1.0);
}

/** The relief, whole angular frequencies and whole multiples of u only. x = angle, y = u. */
float wallField(vec2 q) {
    float a = q.x;
    float u = q.y;
    float s = 0.6 * sin(3.0 * a + 2.0 * u + 0.8 * sin(2.0 * a - 3.0 * u + gPhase));
    s += 0.4 * sin(5.0 * a - u + 1.7 + gPhase * 0.6);
    return s;
}

/** The vortex axis at depth z: the touch drag, plus the drain leaning toward uOrbit ahead of the eye. */
vec2 axisAt(float z) {
    float ahead = 1.0 - exp(-max(z - gEye.z, 0.0) * 0.08);
    return gAxis + gWander * ahead;
}

/** The spiral's angle at depth z: standing twist (one turn per 32), the energy winding from the eye, the spin. */
float windAt(float z) {
    return z * (LV_TAU / 32.0) + gWind * (z - gEye.z) + gSpin;
}

float map(vec3 p) {
    vec2 c = p.xy - axisAt(p.z);
    float r = length(c);
    float ang = r > 1e-4 ? atan(c.y, c.x) : 0.0;
    vec2 q = vec2(ang + windAt(p.z), p.z * LV_UF);
    float wall = gRadius + 0.5 * sin(p.z * (LV_TAU / 32.0) + 0.7) + gRelief * wallField(q);
    float d = (abs(wall - r) - LV_SHELL) / LV_LIP;
    return max(d, LV_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** Value noise whose lattice wraps every `period` cells along z, 0..1. */
float lvNoise(vec3 p, float period) {
    vec3 f = floor(p);
    vec3 t = p - f;
    vec3 w = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    ivec2 i = ivec2(f.xy);
    int z0 = int(mod(f.z, period));
    int z1 = int(mod(f.z + 1.0, period));
    float c000 = hashCell(ivec3(i.x, i.y, z0));
    float c100 = hashCell(ivec3(i.x + 1, i.y, z0));
    float c010 = hashCell(ivec3(i.x, i.y + 1, z0));
    float c110 = hashCell(ivec3(i.x + 1, i.y + 1, z0));
    float c001 = hashCell(ivec3(i.x, i.y, z1));
    float c101 = hashCell(ivec3(i.x + 1, i.y, z1));
    float c011 = hashCell(ivec3(i.x, i.y + 1, z1));
    float c111 = hashCell(ivec3(i.x + 1, i.y + 1, z1));
    float x00 = mix(c000, c100, w.x);
    float x10 = mix(c010, c110, w.x);
    float x01 = mix(c001, c101, w.x);
    float x11 = mix(c011, c111, w.x);
    return mix(mix(x00, x10, w.y), mix(x01, x11, w.y), w.z);
}

/**
 * fbm of lvNoise. Lacunarity is exactly 2 with the period doubled per octave,
 * so every octave still repeats every 32 world units; the octaves are turned
 * in xy only (turning z into xy would break that), which keeps them off one axis.
 */
float lvFbm(vec3 p, int octaves) {
    float sum = 0.0;
    float amp = 0.5;
    float norm = 0.0;
    float period = LV_ZPERIOD;
    for (int o = 0; o < LUCID_MAX_OCTAVES; o++) {
        if (o >= octaves) break;
        sum += amp * lvNoise(p, period);
        norm += amp;
        p = vec3(LUCID_OCT_ROT * p.xy * 2.0 + vec2(13.7, 7.1), p.z * 2.0 + 5.0);
        period *= 2.0;
        amp *= 0.5;
    }
    return sum / max(norm, 1e-4);
}

void main() {
    vec2 uv = view();
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float swell = clamp(SND_ENERGY_TREND * uSoundDrive, 0.0, 1.5);
    float rel = clamp(uEnergyRel - 0.6, 0.0, 1.4) * uIntensity;
    float onset = lucidBump(SND_ONSET_ENV * uSoundDrive) * clamp(uIntensity, 0.0, 2.0);
    float chaos = lucidChaos();
    int depthLevels = lucidDetail(3, 2);
    int oct = lucidDetail(3, 2);

    // Winding per unit of depth: the old 1/r tightening, spent on distance.
    gWind = clamp(0.03 + 0.11 * rel + 0.08 * uTwist, -0.3, 0.3);
    gSpin = mod(t * 0.12, LV_TAU);
    gPhase = mod(t * 0.21, LV_TAU);
    gRelief = LV_RELIEF_MAX * (0.55 + 0.45 * clamp(uMorph, 0.0, 1.0));
    gRadius = LV_RADIUS + 1.1 * kSpace;
    gAxis = touchAnchor() * touchStrength() * 0.9;
    gWander = uOrbit * 1.2 * uIntensity;
    float wl = length(gWander);
    if (wl > 1.5) gWander *= 1.5 / wl;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 32 (see the header), so this
    // shift is exact and invisible, and z keeps its precision.
    ro.z = mod(ro.z, LV_ZWRAP);
    gEye = ro;

    float tm = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < LV_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > LV_FAR) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0008 * tm + 0.0008;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.9, eps);
    }

    float hue0 = 0.15 * kTone;
    float ahead = pow(max(dot(rd, normalize(uCamBasis[2])), 0.0), 6.0);
    // Smoke lit by the walls, and the drain: the bore's far end sinks to black.
    vec3 fogCol = (blackbody(0.3) * 0.10 + pal(hue0 + 0.05) * 0.02) * (1.0 - 0.9 * ahead)
                + vec3(0.004, 0.002, 0.002);
    vec3 col = fogCol;
    float sceneT = hitT > 0.0 ? hitT : LV_FAR;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));

        // The paint, on the hit point only. Turning the wall point by the
        // spiral's angle is seamless all the way round (it is a rotation of
        // Cartesian xy, not an unrolled angle), and every z read is periodic.
        vec2 c = p.xy - axisAt(p.z);
        vec2 q = lucidRot(windAt(p.z)) * c * 0.5;
        float zp = p.z * (LV_TAU / 32.0);
        vec2 zoff = 3.0 * vec2(cos(zp), sin(zp));
        q += lucidJitter(q, t);
        float push = 0.32 * chaos * (1.0 + 1.2 * kFlow);
        for (int i = 0; i < LUCID_MAX_DEPTH; i++) {
            if (i >= depthLevels) break;
            float lv = float(i);
            vec2 off = zoff + vec2(0.0, t * 0.03);
            q = fluidWarp(q + off, 0.7 + 0.45 * lv, push / (1.0 + lv)) - off;
        }
        float zn = p.z * LV_ZCELLS;
        float heat = lvFbm(vec3(q * 1.4, zn), oct);
        float fil = 1.0 - abs(2.0 * lvFbm(vec3(q * 2.3 + 4.1, zn + 3.0), oct) - 1.0);
        fil = pow(fil, 6.0 + 4.0 * kTexture);

        float h = clamp(heat * 0.75 + fil * 0.55 - 0.2 + 0.25 * kBuild, 0.0, 1.0);
        vec3 metal = pal(hue0 + 0.05 + 0.2 * heat);
        vec3 glow = mix(blackbody(h), metal * h * 1.4, 0.35);
        glow = lucidCubic(glow, 0.5);

        // The crust: a dim headlight on the cooled metal, so the relief reads.
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.3, 1.0);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 24.0);
        float fres = pow(1.0 - ndv, 3.0);
        float cool = 1.0 - h;
        vec3 crust = metal * (0.05 + 0.12 * ndv + 0.25 * spec) * cool * ao
                   + pal(hue0 + 0.55) * fres * 0.06 * cool;
        vec3 wall = glow * (0.75 + 0.25 * ndv) + crust;

        col = mix(wall, fogCol, 1.0 - exp(-hitT * 0.055));
    }

    // Embers: points on world z slices every 2 units ahead of the eye, in
    // rings round the axis. Each ring turns rigidly at its own rate, faster
    // near the axis (0.9 / (r + 0.4) rad per second), and holds 6 + 4 * ring
    // whole sectors, so a ring wraps with no seam and the shear lives between
    // rings, never inside one. Slices sit at fixed world z, indexed mod 16.
    if (abs(rd.z) > 0.08) {
        float dir = rd.z > 0.0 ? 1.0 : -1.0;
        float base = rd.z > 0.0 ? floor(ro.z * 0.5) + 1.0 : ceil(ro.z * 0.5) - 1.0;
        float flare = clamp(0.25 + 1.8 * onset * (1.0 + 1.6 * kPulse), 0.0, 3.0);
        for (int i = 0; i < 6; i++) {
            float zi = (base + dir * float(i)) * 2.0;
            float ti = (zi - ro.z) / rd.z;
            if (ti < 0.5 || ti > sceneT) continue;
            vec2 c = ro.xy + rd.xy * ti - axisAt(zi);
            float r = length(c);
            if (r > gRadius - 0.8 || r < 1e-3) continue;
            float ring = floor(r / 0.7);
            float rc = (ring + 0.5) * 0.7;
            float omega = 0.9 / (rc + 0.4);
            float turn = mod(omega * t, LV_TAU);
            float sectors = 6.0 + 4.0 * ring;
            float a = atan(c.y, c.x) - turn;
            float cellA = floor(fract(a / LV_TAU) * sectors);
            int slice = int(mod(zi * 0.5, 16.0));
            float h1 = hashCell(ivec3(int(ring), int(cellA), slice));
            if (h1 < 0.5) continue;
            float h2 = hashCell(ivec3(int(ring), int(cellA), slice + 53));
            float ea = (cellA + 0.25 + 0.5 * h2) * LV_TAU / sectors + turn;
            float er = rc + (h1 - 0.75) * 0.4;
            vec2 e = er * vec2(cos(ea), sin(ea));
            float pxi = ti * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
            float rad = 0.03 + pxi;
            float m = (1.0 - smoothstep(0.0, rad, length(c - e))) * min(1.0, 0.03 / rad);
            float tw = 0.6 + 0.4 * sin(t * (5.0 + 7.0 * h2) + h1 * 40.0);
            vec3 ember = blackbody(0.75 + 0.25 * h2);
            col += ember * m * flare * tw * exp(-ti * 0.055);
        }
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
