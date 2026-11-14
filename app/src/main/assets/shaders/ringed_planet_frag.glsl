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

// Ringed Planet: a banded gas giant with a tilted, gapped ring system, two
// cratered moons, two moonlets shepherding the ring gaps, and a starfield,
// circled on the shared rig (camAround).
//
// WHAT IS DRAWN, built the way the Flame, Opal and Hollows families are. The
// planet (a spheroid), the moons and the moonlets are one distance field,
// sphere-traced and shaded once. The moons' relief is a ridged fractal, rims
// within rims at four scales. The clouds are fractal too: the belts are bent
// by a domain-warped fbm four octaves deep and two finer noises on top, the
// great storm winds them into a spiral, and a chain of eddies a quarter of its
// size curls along each belt edge, alternate ones turning opposite ways. The
// rings are an analytic sheet (thinner than a pixel at any distance the eye
// comes to) whose ringlets nest five octaves deep, each octave faded to its
// average before it is finer than a pixel, with the moonlets' wakes rippling
// the gap edges behind them. The planet's spin axis is the ring normal, so the
// bands run parallel to the rings. Shadows are analytic: the rings shadow the
// planet, and the planet shadows the rings, the moons and the moonlets.
//
// COST. One loop of RP_STEPS steps (80 at the default Detail, 100 at most),
// each one map(): the spheroid, two moon shells (their four noise octaves only
// within 0.1 of the relief) and two spheres. A moon hit adds four map() for
// its normal; the planet's normal is analytic. The clouds cost two fbm3 and
// two value noises per planet pixel, the rings five sines. The palette is read
// five times per pixel (rpSetup) and mixed.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gRpForm    the planet flattens at the poles, as a fast spinner does
//   Warp        gRpFlow    the belts bend into long waves round the planet
//   Turbulence  gRpChaos   the belts boil: their turbulence deepens
//   Intensity   neonGlow() the emission
//   k0 PULSE the emission, k1 TEXTURE the ringlets' line width, k2 FLOW the
//   bend, k3 TONE the palette position, k4 BUILD the atmosphere's and the
//   aurora's glow, k5 SPACE the reach (how far out the eye circles).
//
// THE SOUND, heard the way the families hear it: lib_sound's shaped reads, in
// rpSetup(), on the motion only, never on a colour, a brightness or a beat.
//
//   gRpSwell  the low band, sndShaped(SND_LOW), deepens the Warp's waves in
//   the belts, never past the Warp's own reach.
//
// It is times SND_DRIVE, the music's absolute magnitude: how MUCH moves
// follows how loud and dense the music is, and the band only shapes the push.
// No sound band turns it (owner 2026-11-12, no style spins to the music).
// What moves on a hit is the reaction matrix, through the params above. uTime
// stops in silence and SND_DRIVE falls to 0, so everything here comes to rest.
// Every clock phase is whole cycles per uTime wrap (RP_RATE).
//
// WHY THE ESTIMATE IS SAFE. The spheroid's (|p / r| - 1) * min(r) grows at
// most 1 per unit, so it never overestimates. A moon is |p - c| - rad - A *
// relief, relief a ridged fbm3 (four octaves: |grad| <= 2 * 2.17 * 1.875 per
// noise unit) at RP_CRATER_F noise units per world unit, so its gradient is at
// most 1 + 8.2 A F and the field is divided by that; farther than 0.1 outside
// the relief's shell a moon is just that shell's sphere. The moonlets are
// spheres. The eye circles at RP_CAM_MIN..RP_CAM_MAX by SPACE and the dolly
// pulls it in to 0.65 of that, 4.875 at the closest; every solid lies inside
// RP_BOUND, so the eye is never inside one and a ray that misses that sphere
// is never marched.

#define RP_TAU 6.2831853
#define RP_MAX_STEPS 100
/** uSteps (64..128 by Detail, 102 at its default) on this loop, as the families take it. */
#define RP_STEPS (min(uSteps * 0.78, float(RP_MAX_STEPS)))
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define RP_RATE(k) (RP_TAU * float(k) / 7100.0)
#define RP_R 1.5
#define RP_RIN 1.95
#define RP_ROUT 3.4
#define RP_CAM_MIN 7.5
#define RP_CAM_MAX 9.5
/** The outer moon's centre is at most 4.21 out, its radius 0.3 and its relief 0.02. */
#define RP_BOUND 4.62
/** A moon's relief: the height of its rims, world units, and noise units per world unit. */
#define RP_RELIEF 0.02
#define RP_CRATER_F 7.0
#define RP_MOON_K (1.0 / (1.0 + 8.2 * RP_RELIEF * RP_CRATER_F))
/** The moonlets' radii. */
#define RP_LET_A 0.05
#define RP_LET_B 0.035

float gRpForm;
float gRpFlow;
float gRpChaos;
/** The low band's deepening of the belts' waves, 0..1.5. */
float gRpSwell;
/** The ringlets' line width and the glow, both 1 at rest. */
float gRpLine;
float gRpGlow;
float gRpTone;
vec3 gRpPal0;
vec3 gRpPal1;
vec3 gRpPal2;
vec3 gRpPal3;
vec3 gRpPal4;

/** Ring normal (= the planet's spin axis), two axes in the ring plane, and the unit direction to the sun. */
vec3 gN;
vec3 gT;
vec3 gB;
vec3 gL;
/** How flat the planet is, how far the belts wave, and how much they boil. */
float gObl;
float gWave;
float gTurb;
/** The two gaps, as fractions across the rings. */
vec2 gGaps;
/** The moons (centre, radius, seed) and the moonlets (centre, orbit angle). */
vec3 gMoonA;
float gMoonAR;
float gMoonAH;
vec3 gMoonB;
float gMoonBR;
float gMoonBH;
vec3 gLetA;
vec3 gLetB;
float gLetAngA;
float gLetAngB;
/** Set by map(): 0 the planet, 1 and 2 the moons, 3 a moonlet. */
float gMat;

mat2 rpRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/** The user's palette at t, from the five reads of rpSetup(): there and back, so no seam. */
vec3 rpCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gRpPal0, gRpPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gRpPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gRpPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gRpPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

/** One moon's place on its orbit: its seed sets its size, its orbit and how far off the ring plane it rides. */
vec3 moonAt(float h, float ang) {
    return (gT * cos(ang) + gB * sin(ang)) * (3.85 + 0.35 * h) + gN * (h - 0.5) * 0.5;
}

void rpSetup() {
    gRpForm = neonForm();
    gRpFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gRpChaos = neonChaos();
    gRpLine = 1.0 + neonKnob(1);
    gRpGlow = 1.0 + neonKnob(4);
    gRpTone = 0.6 * neonKnob(3);
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gRpSwell = sndShaped(SND_LOW) * drive;
    gRpPal0 = pal(0.0);
    gRpPal1 = pal(0.25);
    gRpPal2 = pal(0.5);
    gRpPal3 = pal(0.75);
    gRpPal4 = pal(1.0);

    gObl = 0.15 * gRpForm;
    gWave = min(gRpFlow + 0.3 * gRpSwell, 1.0);
    gTurb = 0.18 + 0.5 * gRpChaos;
    gGaps = vec2(0.56, 0.83);

    // The frame: the rings tipped toward the sun, which stands off to one side.
    gN = normalize(vec3(sin(0.45) * cos(1.2), cos(0.45), sin(0.45) * sin(1.2)));
    gT = normalize(cross(gN, vec3(0.0, 0.0, 1.0)));
    gB = cross(gT, gN);
    gL = normalize(vec3(0.55, 0.35, -0.75));

    gMoonAH = 0.35;
    gMoonAR = 0.17 + 0.13 * gMoonAH;
    gMoonA = moonAt(gMoonAH, uTime * RP_RATE(56) + 0.4);
    gMoonBH = 0.8;
    gMoonBR = 0.17 + 0.13 * gMoonBH;
    gMoonB = moonAt(gMoonBH, 3.1 - uTime * RP_RATE(45));
    // The moonlets ride in the gaps, the inner one faster.
    gLetAngA = uTime * RP_RATE(121) + 1.3;
    gLetAngB = uTime * RP_RATE(95) + 4.1;
    gLetA = (gT * cos(gLetAngA) + gB * sin(gLetAngA)) * (RP_RIN + gGaps.x * (RP_ROUT - RP_RIN));
    gLetB = (gT * cos(gLetAngB) + gB * sin(gLetAngB)) * (RP_RIN + gGaps.y * (RP_ROUT - RP_RIN));
}

/** 1 in sunlight, 0 in the planet's shadow, soft at the edge. `p` is outside the planet. */
float planetShadow(vec3 p) {
    float tc = -dot(p, gL);
    if (tc <= 0.0) return 1.0;
    float b = length(p + gL * tc);
    return smoothstep(RP_R * 0.96, RP_R * 1.04, b);
}

/**
 * Ring optical density at Q, a point in the ring plane: ringlets nested five
 * octaves deep, two gaps whose edges ripple behind their moonlets, and soft
 * edges. `px` is a pixel's footprint across the rings: an octave finer than
 * it fades to its average instead of aliasing.
 */
float ringDensityAt(vec3 Q, float px) {
    float r = length(Q);
    float x = (r - RP_RIN) / (RP_ROUT - RP_RIN);
    if (x <= 0.0 || x >= 1.0) return 0.0;
    // r is at least RP_RIN here, so the angle is defined.
    float a = atan(dot(Q, gB), dot(Q, gT));
    float edge = smoothstep(0.0, 0.05, x) * (1.0 - smoothstep(0.97, 1.0, x));
    float sum = 0.0;
    float norm = 0.0;
    float amp = 1.0;
    float f = 19.0;
    for (int k = 0; k < 5; k++) {
        float keep = 1.0 - smoothstep(0.8, 3.0, f * px);
        float rk = pow(0.5 + 0.5 * sin(r * f + float(k) * 1.7 + 0.8 * sin(r * f * 0.37 + float(k))), 2.0 / gRpLine);
        sum += amp * mix(0.45, rk, keep);
        norm += amp;
        f *= 2.37;
        amp *= 0.6;
    }
    float bands = 0.1 + 0.9 * sum / norm;
    // Behind each moonlet its gap's edges ripple: a wave that dies away along the orbit.
    float da = mod(gLetAngA - a, RP_TAU);
    float db = mod(gLetAngB - a, RP_TAU);
    float gapA = smoothstep(0.012, 0.035, abs(x - gGaps.x + 0.01 * sin(da * 24.0) * exp(-da * 1.5)));
    float gapB = smoothstep(0.004, 0.014, abs(x - gGaps.y + 0.004 * sin(db * 30.0) * exp(-db * 1.5)));
    // The inner ring is the faint, dusty one.
    float inner = mix(0.45, 1.0, smoothstep(0.12, 0.22, x));
    return clamp(bands, 0.0, 1.0) * edge * gapA * gapB * inner;
}

/** The spheroid: the planet, flattened along its spin axis by gObl. */
float planetDist(vec3 p) {
    vec3 lp = vec3(dot(p, gT), dot(p, gN), dot(p, gB));
    float c = 1.0 - gObl;
    return (length(lp * vec3(1.0, 1.0 / c, 1.0)) - RP_R) * c;
}

/** A moon's relief, 0..1: a ridged fbm, crater rims within crater rims. */
float moonRelief(vec3 q) {
    return 1.0 - abs(2.0 * fbm3(q, 4) - 1.0);
}

float moonDist(vec3 p, vec3 c, float rad, float h) {
    float shell = length(p - c) - rad - RP_RELIEF;
    if (shell > 0.1) return shell;
    float rel = moonRelief((p - c) * RP_CRATER_F + h * 17.0);
    return (length(p - c) - rad - RP_RELIEF * rel) * RP_MOON_K;
}

float map(vec3 p) {
    float d = planetDist(p);
    gMat = 0.0;
    float dm = moonDist(p, gMoonA, gMoonAR, gMoonAH);
    if (dm < d) {
        d = dm;
        gMat = 1.0;
    }
    dm = moonDist(p, gMoonB, gMoonBR, gMoonBH);
    if (dm < d) {
        d = dm;
        gMat = 2.0;
    }
    float dl = min(length(p - gLetA) - RP_LET_A, length(p - gLetB) - RP_LET_B);
    if (dl < d) {
        d = dl;
        gMat = 3.0;
    }
    return d;
}

/** The field's gradient at p, unit length; straight back along the ray where the field is flat. */
vec3 rpNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec3 g = e.xyy * map(p + e.xyy) + e.yyx * map(p + e.yyx) + e.yxy * map(p + e.yxy) + e.xxx * map(p + e.xxx);
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

float rpDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** The gas giant's surface colour at P, n its outward normal. */
vec3 shadePlanet(vec3 P, vec3 n, vec3 rd) {
    vec3 lp = vec3(dot(P, gT), dot(P, gN), dot(P, gB));
    vec3 sp = lp / max(length(lp), 1.0e-4);
    float lat = sp.y;
    // The belts slide past one another and back.
    // In 3D about the spin axis, so there is no longitude seam.
    float rotA = uTime * RP_RATE(56) + 0.8 * sin(lat * 5.0) * sin(uTime * RP_RATE(20));
    vec2 cs = rpRot(rotA) * sp.xz;
    vec3 q = vec3(cs.x, lat, cs.y);

    // The great storm: a spiral that breathes, riding with its belt.
    vec3 sC = normalize(vec3(0.78, -0.38, 0.5));
    float sr = length(q - sC);
    vec3 qs = rotAxis(sC, 2.6 * (1.0 + 0.3 * sin(uTime * RP_RATE(90))) * exp(-sr * sr * 26.0)) * q;

    // The eddies: the storm's shape again at a quarter of its size, a chain of
    // twelve along each belt edge. Each sits well inside its own cell of
    // longitude, so the cell boundaries never show.
    float ly = lat >= 0.0 ? 0.32 : -0.32;
    float lon = length(qs.xz) > 1.0e-4 ? atan(qs.z, qs.x) : 0.0;
    float cell = floor(lon * (12.0 / RP_TAU));
    float lonC = (cell + 0.3 + 0.4 * hash11(cell + (ly > 0.0 ? 31.0 : 7.0))) * (RP_TAU / 12.0);
    float ry = sqrt(1.0 - ly * ly);
    vec3 eC = vec3(ry * cos(lonC), ly, ry * sin(lonC));
    float er = length(qs - eC);
    // Alternate eddies turn opposite ways, as eddies between two shearing belts do.
    qs = rotAxis(eC, (mod(cell, 2.0) < 0.5 ? 2.0 : -2.0) * exp(-er * er * 380.0)) * qs;

    // The belts: bent by a slower fbm of themselves, then two finer noises on top.
    vec3 wq = qs * vec3(2.2, 7.0, 2.2);
    float w = fbm3(wq * 0.6 + vec3(4.1, 0.0, 1.3), 2);
    float n1 = fbm3(wq + vec3(0.0, 0.0, 1.7 * sin(uTime * RP_RATE(14))) + 1.6 * (w - 0.5) * vec3(1.0, 0.25, 1.0), 4);
    float n2 = vnoise3(qs * 14.0 + 3.1 + 1.5 * n1);
    float n3 = vnoise3(qs * 31.0 - 1.7 + 2.0 * n2);
    float latW = lat + 0.07 * gWave * sin(4.0 * lon + 2.0 * lat) + gTurb * (n1 - 0.5) * 0.3
               + 0.015 * (n2 - 0.5) + 0.008 * (n3 - 0.5);
    float band = 0.5 + 0.5 * sin(latW * 19.0 + 1.7 * sin(latW * 6.0 + 0.5));
    float fine = 0.5 + 0.5 * sin(latW * 47.0 + n1 * 3.0);
    vec3 base = mix(rpCol(gRpTone + 0.05), rpCol(gRpTone + 0.32), band * 0.7 + fine * 0.3);
    base = mix(base, vec3(0.92, 0.86, 0.76), 0.3 * band * fine);
    // The storm and the eddies: their own colour, a dark eye.
    float sm = exp(-sr * sr * 90.0);
    base = mix(base, rpCol(gRpTone + 0.55) * 1.1, sm * 0.75);
    base *= 1.0 - 0.5 * exp(-sr * sr * 900.0);
    base = mix(base, rpCol(gRpTone + 0.55), 0.35 * exp(-er * er * 900.0));

    // Light: a soft terminator, the rings' shadow, limb darkening.
    float day = smoothstep(-0.12, 0.35, dot(n, gL));
    float rs = 0.0;
    float den = dot(gL, gN);
    if (abs(den) > 1.0e-3) {
        float ts = -dot(P, gN) / den;
        if (ts > 0.0) rs = ringDensityAt(P + gL * ts, 0.01) * 0.8;
    }
    float mu = clamp(dot(n, -rd), 0.0, 1.0);
    vec3 col = base * (0.025 + 1.1 * day * (1.0 - rs));
    col *= 0.55 + 0.45 * pow(mu, 0.4);
    col += rpCol(gRpTone + 0.12) * pow(1.0 - mu, 3.0) * (0.12 + 0.8 * day) * gRpGlow;

    // The auroral ovals, fixed to the spin axis (sp, not q), brighter on the night side.
    float al = abs(lat);
    float oval = smoothstep(0.8, 0.9, al) * (1.0 - smoothstep(0.93, 0.995, al));
    float plon = length(sp.xz) > 1.0e-4 ? atan(sp.z, sp.x) : 0.0;
    float cur = 0.5 + 0.5 * sin(plon * 9.0 + uTime * RP_RATE(1017) + 2.5 * sin(plon * 2.0 - uTime * RP_RATE(339)));
    col += rpCol(gRpTone + 0.4) * oval * (0.35 + 0.65 * cur) * 0.5 * gRpGlow * (0.4 + 0.6 * (1.0 - day));
    return col;
}

/** A moon's surface: grey rock tinted by the palette, its rims lit by the marched normal, shadowed by the planet. */
vec3 shadeMoon(vec3 P, vec3 n, vec3 c, float rad, float h) {
    float rel = moonRelief((P - c) * RP_CRATER_F + h * 17.0);
    vec3 base = mix(vec3(0.55, 0.52, 0.5), rpCol(gRpTone + 0.6 + h * 0.3), 0.3) * (0.4 + 0.85 * rel);
    // The terminator from the sphere, the relief's light and shade from the normal.
    float day = smoothstep(-0.05, 0.45, dot((P - c) / rad, gL));
    float dif = 0.35 + 0.65 * max(dot(n, gL), 0.0);
    // The giant's lit face throws a little light on the moon's night side.
    float shine = 0.05 * max(dot(n, -normalize(c)), 0.0);
    return base * (0.015 + shine + 1.05 * day * dif * planetShadow(P));
}

/** Stars, a faint galactic band and the sun. */
vec3 rpSky(vec3 rd) {
    vec3 col = rpCol(gRpTone + 0.7) * 0.006;
    vec3 sc = rd * 128.0;
    float h = hashCell(ivec3(floor(sc)));
    float s = step(0.985, h) * (1.0 - smoothstep(0.0, 0.35, length(fract(sc) - 0.5)));
    float tw = 0.5 + 0.5 * sin(uTime * RP_RATE(3400) + h * 80.0);
    col += mix(vec3(1.0), rpCol(fract(h * 13.0)), 0.3) * s * (0.45 + 0.15 * tw);
    float gal = exp(-abs(dot(rd, normalize(vec3(0.3, 1.0, 0.2)))) * 5.0);
    col += rpCol(gRpTone + 0.6) * 0.035 * gal * (0.5 + 0.5 * vnoise3(rd * 6.0));
    float ca = max(dot(rd, gL), 0.0);
    col += vec3(1.0, 0.92, 0.8) * (3.0 * smoothstep(0.9993, 0.9997, ca) + 0.25 * pow(ca, 64.0) + 0.04 * pow(ca, 6.0));
    return col;
}

/** The light along one ray: the solids sphere-traced inside RP_BOUND and shaded once, the halo, then the rings over them. */
vec3 rpTrace(vec3 ro, vec3 rd) {
    vec3 col = rpSky(rd);
    float n = RP_STEPS;
    float hitT = -1.0;
    float mat = 0.0;
    float bOff = dot(ro, rd);
    float bDisc = bOff * bOff - (dot(ro, ro) - RP_BOUND * RP_BOUND);
    if (bDisc > 0.0) {
        float bRoot = sqrt(bDisc);
        float t = max(-bOff - bRoot, 0.0) + 0.02 * rpDither();
        float tEnd = -bOff + bRoot;
        for (int i = 0; i < RP_MAX_STEPS; i++) {
            if (float(i) >= n || t > tEnd) break;
            float d = map(ro + rd * t);
            float eps = 0.0008 * (1.0 + t);
            if (d < eps) {
                hitT = t;
                mat = gMat;
                break;
            }
            t += max(d, 0.25 * eps);
        }
    }

    if (hitT > 0.0) {
        vec3 P = ro + rd * hitT;
        if (mat < 0.5) {
            // The spheroid's normal, exactly: the gradient of |lp * (1, 1/c, 1)|.
            vec3 lp = vec3(dot(P, gT), dot(P, gN), dot(P, gB));
            float c = 1.0 - gObl;
            vec3 g = gT * lp.x + gN * (lp.y / (c * c)) + gB * lp.z;
            float gl = length(g);
            col = shadePlanet(P, gl > 1.0e-6 ? g / gl : -rd, rd);
        } else {
            vec3 nor = rpNormal(P, rd, 0.0008 * (1.0 + hitT));
            if (mat < 1.5) {
                col = shadeMoon(P, nor, gMoonA, gMoonAR, gMoonAH);
            } else if (mat < 2.5) {
                col = shadeMoon(P, nor, gMoonB, gMoonBR, gMoonBH);
            } else {
                col = vec3(0.55, 0.52, 0.5) * (0.015 + max(dot(nor, gL), 0.0) * planetShadow(P));
            }
        }
    } else {
        // The atmosphere just outside the limb, brighter toward the sun, with the aurora's glow over the poles.
        float tc = max(-dot(ro, rd), 0.0);
        vec3 cp = ro + rd * tc;
        float b = length(cp);
        if (b > RP_R) {
            vec3 cn = cp / b;
            float hz = exp(-(b - RP_R) * 7.0);
            float lit = smoothstep(-0.3, 0.6, dot(cn, gL));
            col += rpCol(gRpTone + 0.12) * hz * (0.04 + 0.3 * lit + 1.2 * pow(max(dot(rd, gL), 0.0), 6.0)) * gRpGlow;
            float pole = smoothstep(0.75, 0.95, abs(dot(cn, gN)));
            col += rpCol(gRpTone + 0.4) * pole * exp(-(b - RP_R) * 14.0) * 0.25 * gRpGlow;
        }
    }

    // ---- the rings, over whatever is behind them ----
    float den = dot(rd, gN);
    if (abs(den) > 1.0e-4) {
        float tr = -dot(ro, gN) / den;
        if (tr > 0.0 && (hitT < 0.0 || tr < hitT)) {
            vec3 Q = ro + rd * tr;
            // A pixel's footprint across the rings, stretched where the sheet is seen edge-on.
            float px = tr * 2.0 / (max(uCamFocal, 0.2) * uResolution.y * max(abs(den), 0.15));
            float dens = ringDensityAt(Q, px);
            if (dens > 0.0) {
                float x = (length(Q) - RP_RIN) / (RP_ROUT - RP_RIN);
                vec3 rc = mix(vec3(0.85, 0.78, 0.66), rpCol(gRpTone + 0.1 + x * 0.25), 0.45);
                // Lit face or seen through; forward scatter toward the sun.
                bool sameSide = dot(gN, gL) * dot(gN, -rd) > 0.0;
                float lit = abs(dot(gN, gL)) * (sameSide ? 1.0 : 0.45) + 0.05;
                lit += 0.6 * pow(max(dot(rd, gL), 0.0), 6.0) * gRpGlow;
                vec3 ringCol = rc * lit * planetShadow(Q) * 1.2;
                // Optical depth grows at grazing angles, as through a real sheet.
                float alpha = 1.0 - exp(-dens * 1.6 / max(abs(den), 0.08));
                col = mix(col, ringCol, clamp(alpha, 0.0, 0.97));
            }
        }
    }
    return col;
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 rpFinish(vec3 col, float exposure) {
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
    rpSetup();
    vec3 ro;
    vec3 rd;
    camAround(uv, mix(RP_CAM_MIN, RP_CAM_MAX, 0.5 + neonKnob(5)), 2.0 * CAM_SPIN_UNIT, ro, rd);
    fragColor = rpFinish(rpTrace(ro, rd), 1.4);
}
