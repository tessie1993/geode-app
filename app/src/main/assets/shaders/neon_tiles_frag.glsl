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
//#include lib_touch

// Neon Tiles: a flight down a kaleidoscope hall. The hall is a regular N-gon
// prism (4, 6 or 8 faces), and its cross-section IS the old kaleidoscope fold:
// folding xy into one mirrored wedge leaves a single wall plane, so every face
// carries the same mirrored tile pattern. The walls are dark glazed tiles
// with the star and cross motifs raised out of them on two lattices (a square
// one and one turned 45 degrees), outlined in neon, with a finer third lattice
// of faint lines between them for the old recursive edges. A refractive glass
// orb hangs at the centre of the view, a little ahead of the eye, and bends
// the real hall behind it (two analytic refractions, then a second march).
// The hall slowly spins and twists one turn per 64 units.
//
// The camera is the shared rig (camFly). Every z term has a period dividing
// 64 (tiles 2, 1 and 0.5; the wall wave 8; the twist 64), so the eye is
// rebased by 64, which divides the rig's 8192 wrap: the wrap is invisible and
// z stays precise.
//
// ---- audio ------------------------------------------------------------------
//
// Treble (SND_HIGH)           neon line width, as in the flat style.
// Bass (SND_BASS_PRESENCE)    the orb's refraction: its index rises, the hall
//                              behind it bends harder.
// Mid (uFlowPhase)            the hall's spin, integrated so it never snaps.
// Kick (SND_KICK_ENV)         the tiles just ahead of the eye push out of the
//                              wall (geometry, not brightness).
// Section (SND_CUT_COUNT)     re-tiles the hall: 4, 6 or 8 faces, the prism
//                              morphing over the section envelope while the
//                              neon dims and relights. uKaleido with
//                              uSymmetry still pins the face count.
// Level (SND_LEVEL)           the neon bloom (col += k col^2).
// Knobs: k0 PULSE kick push, k1 TEXTURE line width, k2 FLOW wall wave,
// k3 TONE the tiles' relief and a small hue offset (a high melody raises the
// stars and crosses out of the wall in deep relief, a low one presses them
// nearly flat; the step bound follows the height), k4 BUILD bloom, k5 SPACE
// hall width (all exactly neutral at 0.5).
// Morph raises the tiles, Warp waves the walls, Turbulence lights the fine lattice.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The fold is an isometry per wedge and picks the nearest face of a convex
// regular polygon, so gR - q.x is the exact distance to the hall's wall from
// inside. Blending two fold counts mixes two 1-Lipschitz fields. The raised
// tiles are a clamp of a 1-Lipschitz 2D distance times NT_BEVEL * height, the
// wall wave adds amplitude * 2pi/8, the kick ramp a little more, the twist a
// factor sqrt(1 + (r k)^2) < 1.04; the field is divided by the sum of those
// bounds (gLipInv). A sphere around the eye is carved out of everything, so
// the eye never sits in a wall, and the orb is analytic.

#define NT_TAU 6.2831853
#define NT_LEVELS 7.0
#define NT_MAX_STEPS 96
#define NT_FAR 44.0
#define NT_ZWRAP 64.0
/** One twist turn per 64 world units: divides the rebase. */
#define NT_TWIST (6.2831853 / 64.0)
/** Inverse width of the tiles' bevel, per world unit. */
#define NT_BEVEL 2.5
#define NT_BUBBLE 0.75
#define NT_ORB_DIST 2.8
#define NT_ORB_R 0.95

float gFoldA;
float gFoldB;
float gBlend;
float gSpin;
float gR;
float gH;
float gWave;
float gKick;
float gLipInv;
vec3 gEye;

vec2 kaleido(vec2 p, float n) {
    float w = NT_TAU / n;
    float a = mod(atan(p.y, p.x), w);
    a = abs(a - 0.5 * w);
    return length(p) * vec2(cos(a), sin(a));
}

float ndot(vec2 a, vec2 b) {
    return a.x * b.x - a.y * b.y;
}

float sdRhombus(vec2 p, vec2 b) {
    p = abs(p);
    float h = clamp(ndot(b - 2.0 * p, b) / dot(b, b), -1.0, 1.0);
    float d = length(p - 0.5 * b * vec2(1.0 - h, 1.0 + h));
    return d * sign(p.x * b.y + p.y * b.x - b.x * b.y);
}

float sdBox2(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdStar(vec2 p, float r) {
    return min(sdRhombus(p, vec2(r, r * 0.35)), sdRhombus(p, vec2(r * 0.35, r)));
}

float sdCrossShape(vec2 p, float w, float l) {
    return min(sdBox2(p, vec2(l, w)), sdBox2(p, vec2(w, l)));
}

// x = world distance to the nearest motif edge, y = 1 for the star, 0 for the cross.
// The lattice period is 1 / scale world units.
vec2 tiles(vec2 p, float scale) {
    vec2 c = fract(p * scale) - 0.5;
    float star = sdStar(c, 0.42) / scale;
    float crs = sdCrossShape(c, 0.06, 0.45) / scale;
    return star < crs ? vec2(star, 1.0) : vec2(crs, 0.0);
}

// A 45-degree lattice whose cell is 1/sqrt(2): its period along z is exactly 1.
vec2 tilesTurned(vec2 p) {
    vec2 r = mat2(0.70710678, -0.70710678, 0.70710678, 0.70710678) * p;
    return tiles(r + 0.25, 1.41421356);
}

// The two motif lattices of the flat style: cells 2 and 1/sqrt(2), z periods 2 and 1.
vec2 motif(vec2 w) {
    vec2 a = tiles(w, 0.5);
    vec2 b = tilesTurned(w);
    return a.x < b.x ? a : b;
}

/** xy into the hall's spinning, twisting frame. */
vec2 hallXY(vec3 p) {
    return rot2(gSpin + p.z * NT_TWIST) * p.xy;
}

/** x = signed distance to the wall (positive inside), yz = wall coordinates of the dominant fold. */
vec3 wallFrame(vec3 p) {
    vec2 xy = hallXY(p);
    vec2 qa = kaleido(xy, gFoldA);
    vec2 qb = kaleido(xy, gFoldB);
    float wave = gWave * sin(p.z * (NT_TAU / 8.0));
    float plane = gR + wave - mix(qa.x, qb.x, gBlend);
    vec2 q = gBlend < 0.5 ? qa : qb;
    return vec3(plane, q.y, p.z);
}

float map(vec3 p) {
    vec3 f = wallFrame(p);
    vec2 m = motif(f.yz);
    // Only the tiles just ahead of the eye answer the kick.
    float push = 1.0 + gKick * exp(-abs(p.z - gEye.z - 3.0) * 0.3);
    float lift = gH * push * clamp(-m.x * NT_BEVEL, 0.0, 1.0);
    float d = (f.x - lift) * gLipInv;
    return max(d, NT_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

float marchHall(vec3 ro, vec3 rd, float tMax, float budget, out float used) {
    used = 0.0;
    float t = 0.02;
    for (int i = 0; i < NT_MAX_STEPS; i++) {
        // budget is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= budget) break;
        if (t > tMax) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0007 * t + 0.0006;
        if (d < eps) return t;
        t += max(d, eps);
    }
    return -1.0;
}

float neon(float d, float w) {
    return smoothstep(w, 0.0, abs(d)) + 0.25 * smoothstep(w * 6.0, 0.0, abs(d));
}

float gWidth;
float gHue;
float gRelight;

/** The neon on the wall at a point: three lattices, the coarse ones the motifs. */
vec3 wallNeon(vec3 p, float px) {
    vec2 w = wallFrame(p).yz;
    float lw = max(gWidth, px * 1.5);
    vec3 col = vec3(0.0);
    for (int i = 0; i < 3; i++) {
        vec2 m;
        float cell;
        if (i == 0) {
            m = motif(w);
            cell = 2.0;
        } else if (i == 1) {
            m = tiles(w + vec2(0.5, 0.25), 1.0);
            cell = 1.0;
        } else {
            m = tiles(w + vec2(0.25, 0.125), 2.0);
            cell = 0.5;
        }
        float hue = m.y > 0.5 ? mix(0.61, 0.89, fract(0.37 * float(i) + 0.1 * sin(uTime * 0.11))) : 0.33;
        float glow = neon(m.x, lw / (1.0 + 0.4 * float(i))) / (1.0 + 0.6 * float(i));
        // A lattice finer than a few pixels would only alias: fade it out.
        float keep = 1.0 - smoothstep(0.06 * cell, 0.2 * cell, px);
        float weight = i == 2 ? 0.35 + 0.9 * clamp(uTurbulence, 0.0, 1.5) : 1.0;
        col += pal(hue + gHue) * glow * keep * weight;
    }
    return col * gRelight;
}

vec3 fogColour(vec3 rd) {
    float ahead = pow(max(rd.z, 0.0), 10.0);
    return pal(0.75 + gHue) * 0.03 + pal(0.61 + gHue) * 0.14 * ahead + vec3(0.003, 0.002, 0.006);
}

/** A cheap stand-in for the hall seen in a reflection: the round tube inscribed in it. */
vec3 hallEnv(vec3 o, vec3 d, float pxScale) {
    float a = dot(d.xy, d.xy);
    vec3 fog = fogColour(d);
    if (a < 1e-4) return fog;
    float r = gR * 0.97;
    float b = dot(o.xy, d.xy);
    float c = dot(o.xy, o.xy) - r * r;
    float h = max(b * b - a * c, 0.0);
    float t = max((-b + sqrt(h)) / a, 0.0);
    vec3 neonCol = wallNeon(o + d * t, t * pxScale);
    return mix(neonCol * 0.7 + pal(0.7 + gHue) * 0.02, fog, 1.0 - exp(-t * 0.06));
}

vec3 shadeHall(vec3 ro, vec3 rd, float t, float used, float budget, float pxScale) {
    vec3 fog = fogColour(rd);
    if (t < 0.0) return fog;
    vec3 p = ro + rd * t;
    vec3 n = normalAt(p, max(0.0015, 0.0012 * t));
    float ndv = clamp(dot(n, -rd), 0.0, 1.0);
    float fres = pow(1.0 - ndv, 3.0);
    float ao = clamp(1.0 - used / max(budget, 1.0) * 1.1, 0.3, 1.0);
    vec3 em = wallNeon(p, t * pxScale);
    // Glazed tile: the neon lights it from inside, the far glow glints on it.
    vec3 axis = normalize(vec3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(reflect(rd, n), axis), 0.0), 24.0);
    vec3 base = pal(0.70 + gHue) * (0.025 + 0.05 * ndv) * ao;
    vec3 col = base + em * (0.55 + 0.45 * ndv) + pal(0.61 + gHue) * spec * 0.25 * ao
             + fogColour(reflect(rd, n)) * fres * 0.6;
    return mix(col, fog, 1.0 - exp(-t * 0.06));
}

void main() {
    vec2 uv = view();
    float bassA = clamp(SND_BASS_PRESENCE, 0.0, 1.3);
    float trebA = clamp(sndShaped(SND_HIGH), 0.0, 1.3);
    float enA = clamp(sndShaped(SND_LEVEL), 0.0, 1.3);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);

    float k0 = uKnob[0] - 0.5;
    float k1 = uKnob[1] - 0.5;
    float k2 = uKnob[2] - 0.5;
    float k3 = uKnob[3] - 0.5;
    float k4 = uKnob[4] - 0.5;
    float k5 = uKnob[5] - 0.5;

    // Faces: pinned by Symmetry when Kaleido is on, else 4, 6 or 8 per
    // section, crossfaded over the section envelope.
    float sw = 1.0 - clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    sw = sw * sw * (3.0 - 2.0 * sw);
    if (uKaleido > 0.5 && uSymmetry >= 2.0) {
        gFoldA = max(uSymmetry, 3.0);
        gFoldB = gFoldA;
        gBlend = 1.0;
        gRelight = 1.0;
    } else {
        gFoldA = 4.0 + 2.0 * float(uhash(uint(mod(SND_CUT_COUNT - 1.0, 4096.0)) + 0x51du) % 3u);
        gFoldB = 4.0 + 2.0 * float(uhash(uint(mod(SND_CUT_COUNT, 4096.0)) + 0x51du) % 3u);
        gBlend = sw;
        // The neon dims while the hall reshapes and relights on the new tiling.
        gRelight = gFoldA == gFoldB ? 1.0 : 0.25 + 0.75 * abs(2.0 * sw - 1.0);
    }

    // Integrated spin: `uTime * rate(mid)` would jump the hall round whenever
    // the mid band moved.
    gSpin = uFlowPhase * 1.6 + uTime * 0.03;
    gR = 2.2 * (1.0 + 0.3 * k5);
    gH = (0.06 + 0.1 * clamp(uMorph, 0.0, 1.0)) * (1.0 + 1.6 * k3);
    gWave = max(0.0, 0.12 * clamp(uWarp, 0.0, 1.5) + 0.3 * k2);
    gKick = 1.2 * kick * clamp(1.0 + 2.0 * k0, 0.0, 2.0);
    float liftMax = gH * (1.0 + gKick);
    // Bevel slope, wave slope, the kick ramp's z slope, and the twist's stretch.
    gLipInv = 1.0 / ((1.0 + liftMax * NT_BEVEL + gWave * (NT_TAU / 8.0) + gH * gKick * 0.3) * 1.04);
    gWidth = mix(0.035, 0.1, clamp(trebA, 0.0, 1.0)) * clamp(1.0 + 1.2 * k1, 0.3, 2.0);
    gHue = 0.15 * k3;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64, so this rebase is exact
    // and invisible (power of two), and z keeps its precision.
    ro.z = mod(ro.z, NT_ZWRAP);
    gEye = ro;
    float pxScale = 2.0 / (max(uCamFocal, 0.1) * uResolution.y);

    // The orb: analytic, at the centre of the view.
    vec3 oc = ro - (ro + uCamBasis[2] * NT_ORB_DIST);
    float ob = dot(oc, rd);
    float occ = dot(oc, oc) - NT_ORB_R * NT_ORB_R;
    float oh = ob * ob - occ;
    float tOrb = oh > 0.0 ? -ob - sqrt(oh) : -1.0;

    float used;
    float tMax = tOrb > 0.0 ? tOrb : NT_FAR;
    float t = marchHall(ro, rd, tMax, uSteps, used);
    vec3 col;
    if (tOrb > 0.0 && t < 0.0) {
        vec3 c = ro + uCamBasis[2] * NT_ORB_DIST;
        vec3 p1 = ro + rd * tOrb;
        vec3 n1 = (p1 - c) / NT_ORB_R;
        float ior = mix(1.3, 1.85, bassA / 1.3);
        vec3 d1 = refract(rd, n1, 1.0 / ior);
        vec3 p2 = p1 + d1 * max(-2.0 * dot(p1 - c, d1), 0.0);
        vec3 n2 = (p2 - c) / NT_ORB_R;
        vec3 d2 = refract(d1, -n2, ior);
        if (dot(d2, d2) < 0.5) d2 = reflect(d1, -n2);
        float budget2 = uSteps * 0.6;
        float used2;
        float t2 = marchHall(p2, d2, NT_FAR, budget2, used2);
        vec3 through = shadeHall(p2, d2, t2, used2, budget2, pxScale);
        vec3 refl = hallEnv(p1, reflect(rd, n1), pxScale);
        float fres = pow(1.0 - max(dot(n1, -rd), 0.0), 3.0);
        vec3 orb = mix(through, refl, fres) * 1.15 + pal(0.75 + gHue) * 0.05;
        // Anti-aliased rim: the ray's miss distance from the sphere, in pixels.
        float miss = sqrt(max(dot(oc, oc) - ob * ob, 0.0));
        float edge = clamp((NT_ORB_R - miss) / max(tOrb * pxScale, 1e-4), 0.0, 1.0);
        col = mix(hallEnv(ro, rd, pxScale), orb, edge);
    } else {
        col = shadeHall(ro, rd, t, used, uSteps, pxScale);
    }

    col = floor(col * NT_LEVELS + 0.5) / NT_LEVELS;
    col += clamp(0.25 + 0.4 * enA + 0.3 * k4, 0.0, 1.0) * col * col;
    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.7 + 0.3 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
