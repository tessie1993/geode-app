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
//#include lib_chrys

// Chrysanthemum Phyllo: the composite flower head, now a solid you can walk
// around. A sunflower disk of golden-angle florets (a Vogel spiral, so the
// 21/34/55 parastichies appear on their own) stands out of a shallow, convex
// receptacle as ~700 small lit domes, young and closed at the centre, full at
// the rim, and two rings of ray petals (21 behind, 34 in front) leave the rim
// as thin, cupped, drooping blades. A low raking key light from one side runs
// across the domes, so the spirals read as ridges of light and shadow, which
// the flat style could only paint.
//
// ---- camera -------------------------------------------------------------------
//
// camAround: the head is one subject, centred on the origin with its face
// tipped toward the rig's starting side (PH_AXIS), so the orbit opens face on,
// grazes the disk edge-on and sweeps round to the pale undersides and the
// receptacle's back. The spin is an integer multiple of CAM_SPIN_UNIT, so the
// rig's 8192 travel wrap lands on a whole turn and never shows; nothing in the
// field reads z or the travel, so there is no other period to fit. Space (k5)
// and the rig's dolly move the eye in and out; one bounding-sphere test comes
// first, so rays that miss the flower never march.
//
// ---- audio --------------------------------------------------------------------
//
// Every onset SEEDS a new constellation, as before: SND_ONSET_COUNT re-hashes
// which florets are "awake" (about one in nine), and those wear the contrasting
// hue until the next onset picks another set. The onset envelope (SND_ONSET_ENV)
// now POPS them out of the disk: an awake floret rises and swells on a
// chrysBump of the envelope, so each hit is a scatter of domes lifting from
// rest and settling back, geometry first, light second. A slow hue wave runs
// outward along the floret index on uFlowPhase (louder passages run it faster,
// never backwards). The sub band (sndSpectrum(0.10)) loosens the spiral's
// spacing a few percent, so the disk breathes wider on the low end. uBreath
// droops and lifts the ray petals; it no longer turns them (owner 2026-11-12,
// no style spins to the music).
// Brightness follows SND_ENERGY_TREND only.
//
// ---- macros -------------------------------------------------------------------
//
// Speed: uTime drives the petal turn and hue clocks (already Speed-integrated).
// Complexity: uMorph packs the disk tighter, up to twice the florets (~1440) in
// the same disk. Chaos: uTurbulence + uWarp fray the ray petals and vary their
// lengths; uTwist sweeps them into a pinwheel. Energy: uIntensity scales the
// onset pop and the sub-band loosening. Colour: pal() throughout. Detail:
// uSteps bounds the march. Knobs (offsets from 0.5, so off is the tuned look):
// k0 PULSE the pop height, k1 TEXTURE the petal fray, k2 FLOW the hue wave's
// depth, k3 TONE the hue, k4 BUILD the awake florets' inner glow, k5 SPACE the
// orbit radius.
//
// ---- why the distance estimate is safe ------------------------------------------
//
// Florets: phFloret() reads the same Fibonacci-pair lattice chrysFloret does
// (see lib_chrys: the crossings of the two visible parastichy families are
// exactly the florets), and measures the TRUE 3D sphere distance to each of
// the four lattice corners around the point, keeping the least. Those four
// are the floret under the point and its neighbours along both spirals, so a
// popped neighbour, taller or wider, is measured itself rather than guessed
// from its neighbour. Outside the floret band (above the tallest possible
// dome, below the lowest, or past the rim) the field is the distance to that
// band's boundary, which every floret lies beyond; inside it, a step is also
// capped at half a floret spacing, so the rare floret the four-corner search
// misses where the lattice changes Fibonacci pair is never stepped over.
// Receptacle: an exact rounded cylinder cut by the dome y = base(r), whose
// slope is at most 2 * PH_DH / R = 0.16, so the cut is divided by 1.02.
// Petals: chrysPetal's 2D distance (a bound, see lib_chrys) intersected with a
// thin slab around the petal surface; that surface's slope is at most ~0.95
// (rise 0.12, droop 2 * 0.4 * 0.95, cup 0.3 * |v|), so the slab is divided by
// PH_PLIP. The march steps 0.7 of the field on top of all that.

#define PH_MAX_STEPS 110
#define PH_TAU 6.2831853
/** Disk radius at rest, world units. */
#define PH_DISK 1.0
/** Height of the receptacle's dome at its centre above its rim. */
#define PH_DH 0.08
/** Half-thickness of a ray petal. */
#define PH_THICK 0.008
/** Gradient bound of the petal slab (see the header). */
#define PH_PLIP 1.4
/** Bounding sphere of the whole head, petals at full chaos included. */
#define PH_BOUND 2.15

/** The face's normal, tipped toward the rig's starting side (-z) and up. */
const vec3 PH_AXIS = vec3(0.0, 0.55, -0.8351647);
/** The third axis of the flower frame, orthogonal to x and PH_AXIS. */
const vec3 PH_SIDE = vec3(0.0, 0.8351647, 0.55);

float gSp;      // floret spacing
float gN;       // floret count
float gRr;      // disk radius (the outermost floret's radius)
float gPop;     // the onset pop, 0..~1.5
float gSeed;    // the constellation seed
float gChaos;
float gFray;
float gDroop;
float gPetRot;

float gMat;     // 0 receptacle, 1 floret, 2 back ring, 3 front ring
float gK;       // floret index
float gAwake;
float gDome;    // 0 at a floret's equator .. 1 at its crown
vec4 gPet;      // the petal's (d, u, v, id)

vec3 toFlower(vec3 p) {
    return vec3(p.x, dot(p, PH_AXIS), dot(p, PH_SIDE));
}

vec3 fromFlower(vec3 q) {
    return vec3(q.x, 0.0, 0.0) + PH_AXIS * q.y + PH_SIDE * q.z;
}

/** The receptacle's convex top at radius r: PH_DH at the centre, 0 at the rim. */
float phBase(float r) {
    float x = min(r / gRr, 1.0);
    return PH_DH * (1.0 - x * x);
}

/**
 * The florets as domes: chrysFloret's lattice, each of the four corners
 * measured as a 3D sphere (see the header). Writes gK, gAwake and gDome for
 * the nearest one.
 */
float phFloret(vec3 q) {
    vec2 p = q.xz;
    float sp = gSp;
    float r = length(p);
    float u = (r / sp) * (r / sp);
    float th = atan(p.y, p.x);
    float n = floor(log(max(2.37 * sqrt(u) * 2.2360680, 2.0)) / log(CHRYS_PHI));
    n = max(n, 3.0);
    float f1 = floor(pow(CHRYS_PHI, n) / 2.2360680 + 0.5);
    float f2 = floor(pow(CHRYS_PHI, n + 1.0) / 2.2360680 + 0.5);
    float d1 = f1 * CHRYS_GOLDEN - CHRYS_TAU * floor(f1 * CHRYS_GOLDEN / CHRYS_TAU + 0.5);
    float d2 = f2 * CHRYS_GOLDEN - CHRYS_TAU * floor(f2 * CHRYS_GOLDEN / CHRYS_TAU + 0.5);
    float det = f2 * d1 - f1 * d2;
    vec2 s0 = floor(vec2(f1 * th - d1 * u, f2 * th - d2 * u) / CHRYS_TAU);
    float best = 1e9;
    for (int j = 0; j < 4; j++) {
        vec2 ab = s0 + vec2(float(j - 2 * (j / 2)), float(j / 2));
        float ku = CHRYS_TAU * (f1 * ab.y - f2 * ab.x) / det;
        if (ku < -0.5 || ku > gN + 0.5) continue;
        float kt = CHRYS_TAU * (ab.y * d1 - ab.x * d2) / det;
        float k = floor(ku + 0.5);
        float rc = sp * sqrt(max(ku, 0.0));
        vec2 c = rc * vec2(cos(kt), sin(kt));
        float age = clamp(k / gN, 0.0, 1.0);
        float aw = step(0.89, chrysHash(vec2(k, gSeed)));
        float rad = sp * (0.5 + 0.32 * age) * (1.0 + 0.3 * gPop * aw);
        float yc = phBase(rc) - 0.4 * rad + 0.9 * sp * gPop * aw;
        vec3 off = vec3(p.x - c.x, q.y - yc, p.y - c.y);
        float d = length(off) - rad;
        if (d < best) {
            best = d;
            gK = k;
            gAwake = aw;
            gDome = clamp(off.y / rad, 0.0, 1.0);
        }
    }
    return best;
}

/** Both rings of ray petals. Writes gPet and the ring's material when nearer than `dIn`. */
float phPetals(vec3 q, float dIn) {
    float d = dIn;
    vec2 pr = chrysTurn(gPetRot) * q.xz;
    float r0 = gRr * 0.96;
    for (int i = 0; i < 2; i++) {
        float k = float(i);
        float count = k < 0.5 ? 21.0 : 34.0;
        float len = k < 0.5 ? 0.68 : 0.58;
        vec2 q2 = chrysTurn(chrysRingOffset(k, count)) * pr;
        float rho = length(q2);
        float a = atan(q2.y, q2.x) - uTwist * (rho - r0);
        float sector = CHRYS_TAU / count;
        float pid = floor(a / sector + 0.5);
        len *= 1.0 + 0.18 * gChaos * chrysWobble(mod(pid, count) + 5.0 * k, uTime);
        vec4 pet = chrysPetal(q2, count, r0, len, 0.5, uTwist, 0.15, 0.35);
        pet.x += chrysFray(q2, gFray);
        float across = (a - pid * sector) * rho;
        float s = max(rho - r0, 0.0);
        // The back ring sits a little lower, tucked under the front one.
        float yb = -0.012 - 0.03 * (1.0 - k);
        float yP = yb + 0.12 * s - gDroop * s * s + 0.3 * across * clamp(pet.z, -1.0, 1.0);
        float dp = max(pet.x, (abs(q.y - yP) - PH_THICK) / PH_PLIP);
        if (dp < d) {
            d = dp;
            gMat = 2.0 + k;
            gPet = pet;
        }
    }
    return d;
}

float map(vec3 p) {
    vec3 q = toFlower(p);
    float rho = length(q.xz);

    // The receptacle: a rounded cylinder from y = -0.2 up past the dome, cut by it.
    vec2 w = vec2(rho - (gRr - 0.08), abs(q.y + 0.06) - 0.14);
    float dCyl = min(max(w.x, w.y), 0.0) + length(max(w, 0.0)) - 0.08;
    float d = max(dCyl, (q.y - phBase(rho)) / 1.02);
    gMat = 0.0;

    // The floret band: every dome lies inside it, so outside it the boundary is a bound.
    float sp = gSp;
    float topF = PH_DH + sp * (0.6 + 0.9 * gPop);
    float dBand = max(max(q.y - topF, -q.y - 1.6 * sp), rho - gRr - 1.2 * sp);
    float dF = dBand;
    if (dBand < 0.5 * sp) {
        dF = min(phFloret(q), max(dBand, 0.5 * sp));
    }
    if (dF < d) {
        d = dF;
        gMat = 1.0;
    }

    // Petals only near the rim: inside 0.8 R the disk always hides them.
    if (rho > gRr * 0.8 - 0.05) d = phPetals(q, d);
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

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float onset = clamp(SND_ONSET_ENV * uIntensity, 0.0, 1.5);
    float sub = clamp(sndSpectrum(0.10) * uIntensity, 0.0, 1.5);

    gChaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    gFray = gChaos * clamp(1.0 + 1.6 * kTexture, 0.2, 1.8);
    gN = 720.0 * (1.0 + clamp(uMorph, 0.0, 1.0));
    gSp = PH_DISK / sqrt(gN) * (1.0 + 0.06 * sub);
    gRr = gSp * sqrt(gN);
    // A bump of the envelope, so a popped floret rises from rest, never snaps.
    gPop = clamp(chrysBump(min(onset, 1.0)) * (1.0 + 0.5 * step(1.0, onset)) * (1.0 + kPulse), 0.0, 1.5);
    gSeed = floor(SND_ONSET_COUNT) * 1.618;
    float breath = clamp(uBreath - 1.0, -1.0, 1.0);
    gDroop = 0.25 + 0.15 * breath;
    gPetRot = t * 0.01;
    float hue = 0.08 + 0.02 * sin(t * 0.05) + 0.12 * kTone;

    vec3 ro;
    vec3 rd;
    camAround(uv, 3.9 * (1.0 + 0.25 * kSpace), 2.0 * CAM_SPIN_UNIT, ro, rd);

    float rUv = length(uv);
    vec3 bg = mix(pal(hue + 0.62) * 0.03, pal(hue + 0.5) * 0.08, exp(-rUv * 1.2));
    vec3 col = bg;

    float b = dot(ro, rd);
    float h = b * b - (dot(ro, ro) - PH_BOUND * PH_BOUND);
    if (h > 0.0) {
        float sq = sqrt(h);
        float tm = max(-b - sq, 0.01);
        float tEnd = -b + sq;
        float hitT = -1.0;
        for (int i = 0; i < PH_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (tm > tEnd) break;
            float d = map(ro + rd * tm);
            float eps = 0.0006 * tm + 0.0004;
            if (d < eps) {
                hitT = tm;
                break;
            }
            tm += max(d * 0.7, eps);
        }

        if (hitT > 0.0) {
            vec3 p = ro + rd * hitT;
            vec3 n = normalAt(p, 0.0008 * hitT + 0.0004);
            // Two short AO taps: the gaps between domes and the petal roots darken.
            float occ = (0.02 - map(p + n * 0.02)) + 0.6 * (0.05 - map(p + n * 0.05));
            float ao = clamp(1.0 - 9.0 * occ, 0.3, 1.0);
            // Restore the hit point's globals after the normal and AO taps.
            map(p);
            vec3 q = toFlower(p);
            float rho = length(q.xz);

            // A thin petal has two faces; light the one we are looking at.
            vec3 nf = dot(n, rd) > 0.0 ? -n : n;
            // The raking key: low across the face from one side, so the spirals cast ridges.
            vec3 L = normalize(fromFlower(vec3(-0.7, 0.45, 0.55)));
            float dif = max(dot(nf, L), 0.0);
            float through = max(dot(-nf, L), 0.0);
            float fill = 0.5 + 0.5 * dot(nf, PH_AXIS);
            float spec = pow(max(dot(reflect(rd, nf), L), 0.0), 28.0);
            float lit = 0.12 + 0.95 * dif + 0.18 * fill;

            if (gMat < 0.5) {
                // The receptacle: dark gaps on top, a muted back.
                vec3 gap = pal(hue + 0.9) * 0.05;
                vec3 back = pal(hue + 0.62) * 0.12;
                col = mix(back, gap, step(0.0, q.y)) * (0.4 + 0.8 * lit);
            } else if (gMat < 1.5) {
                float age = clamp(gK / gN, 0.0, 1.0);
                float wave = (0.06 + 0.1 * kFlow) * sin(gK * 0.035 * 720.0 / gN - uFlowPhase * 0.8);
                vec3 fc = mix(pal(hue + 0.92 + wave) * 0.25, pal(hue + 0.02 + wave) * 0.9, smoothstep(0.0, 0.8, age));
                fc = mix(fc, pal(hue + 0.45 + wave) * 0.95, gAwake);
                col = fc * lit * mix(0.55, 1.0, gDome);
                col += mix(vec3(1.0), fc, 0.5) * spec * 0.35;
                // An awake floret glows from inside while it is up.
                col += pal(hue + 0.45 + wave) * gAwake * gPop * 0.25 * clamp(1.0 + 1.5 * kBuild, 0.0, 2.5);
            } else {
                float k = gMat - 2.0;
                float count = k < 0.5 ? 21.0 : 34.0;
                float wob = chrysHash1(mod(gPet.w, count) + 7.0 * k);
                vec3 pc = mix(pal(hue + 0.03 * wob) * 0.7, pal(hue + 0.06) * 1.05, clamp(gPet.y, 0.0, 1.0))
                        * chrysPetalShade(gPet, 0.2);
                pc *= 0.75 + 0.25 * wob;
                col = pc * lit + pc * through * 0.5 + vec3(spec) * 0.1;
                // The disk shades the petal roots.
                col *= 1.0 - 0.35 * exp(-abs(rho - gRr) * 30.0);
            }
            col *= ao;
            col = mix(col, bg, smoothstep(3.0, 6.5, hitT) * 0.5);
        }
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
