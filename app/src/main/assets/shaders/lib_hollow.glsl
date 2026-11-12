// The Hollows family: flights through the inside of things, sinew tunnels,
// carved caves, ribs, bone; dark walls under blacklight, their carving glowing.
//
// WHAT IS DRAWN. Each style gives one distance to its walls, hollowMap(p),
// positive in the open air, and a relief coordinate beside it. The library
// cuts a fine groove into the wall at every whole number of that coordinate
// and lights the groove, so the carving is real relief that the light catches
// and that glows by itself, the way paint does under a blacklight. The ray is
// sphere-traced to the wall and shaded once: a lamp at the eye that falls off
// with distance, a rim light on every edge that turns away, one cheap
// occlusion tap, the glowing grooves, and a depth haze whose hue runs through
// the palette with distance, so far walls are a different colour to near ones.
//
// COST. One loop of min(uSteps, 72) steps, each one hollowMap; the hit adds
// four for its normal and one for occlusion, so at most 77 hollowMap per
// pixel. The maps are tubes, lattice terms and smooth unions; no noise octaves
// and no texture. The palette is read five times per pixel before the loop
// (hollowSetup) and mixed after.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gHollowForm   the place's own metamorphosis, named by each style
//   Warp        gHollowFlow   hollowBend(): the passage twists about the way
//                             ahead and its walls swell and pinch; the way
//                             itself stays open
//   Turbulence  gHollowChaos  a ripple over the walls
//   Intensity   neonGlow()    the emission
//   k0 PULSE the emission, k1 TEXTURE the groove's depth, k2 FLOW the bend,
//   k3 TONE the palette position, k4 BUILD the grooves' glow, k5 SPACE the reach.
//
// No style here reads the sound: what moves on a hit is the reaction matrix,
// through those params. uTime stops in silence, and so does everything here.
//
// The world repeats along z every HOLLOW_PERIOD (16, dividing the rig's 8192),
// and every clock phase is whole cycles per uTime wrap (HOLLOW_RATE). The eye
// is held inside gHollowBore of the axis, where every style keeps open air.
//
// INCLUDE ORDER: last, after lib_scene_uniforms (with VIEW_OWNS_SHAPE),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon.
// The style defines hollowMap after it.

#define HOLLOW_TAU 6.2831853
#define HOLLOW_MAX_STEPS 72
#define HOLLOW_PERIOD 16.0
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define HOLLOW_RATE(k) (HOLLOW_TAU * float(k) / 7100.0)

float gHollowForm;
float gHollowFlow;
float gHollowChaos;
/** How far the trace reaches, world units. */
float gHollowFar;
/** The groove's depth in the wall, world units, and how brightly it glows. */
float gHollowGroove;
float gHollowGlow;
float gHollowTone;
/** The share of a step taken: under 1 where a style's distance is only a bound. */
float gHollowStride;
/** How far from the axis the eye may ride. */
float gHollowBore;
vec3 gHollowPal[5];

/** The style's walls at p: x the distance to the nearest one (positive in the air), y the relief coordinate (a groove at every whole number). */
vec2 hollowMap(vec3 p);

mat2 hollowRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/** Smooth union of two airs' walls: the smaller distance, rounded where they meet. */
float hollowSmin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

float hollowSmax(float a, float b, float k) {
    return -hollowSmin(-a, -b, k);
}

/** The user's palette at t, from the five reads of hollowSetup(): there and back, so no seam. */
vec3 hollowCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    int i = int(min(floor(x), 3.0));
    return mix(gHollowPal[i], gHollowPal[i + 1], smoothstep(0.0, 1.0, x - float(i)));
}

void hollowSetup() {
    gHollowForm = neonForm();
    gHollowFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gHollowChaos = neonChaos();
    gHollowFar = 14.0 * (1.0 + 0.4 * neonKnob(5));
    gHollowGroove = 0.03 * (1.0 + neonKnob(1));
    gHollowGlow = 1.0 + neonKnob(4);
    gHollowTone = 0.6 * neonKnob(3);
    gHollowStride = 0.7;
    gHollowBore = 0.3;
    for (int i = 0; i < 5; i++) gHollowPal[i] = pal(0.25 * float(i));
}

/** Warp on space, about the axis so the way ahead stays open: a twist that comes and goes, and a swell and pinch. */
vec3 hollowBend(vec3 p) {
    float zc = p.z * (HOLLOW_TAU / HOLLOW_PERIOD);
    float r = length(p.xy);
    p.xy = hollowRot(gHollowFlow * 1.3 * sin(zc + 0.7 * r + uTime * HOLLOW_RATE(140))) * p.xy;
    p.xy *= 1.0 + gHollowFlow * 0.35 * sin(3.0 * zc + 2.0 * p.x + uTime * HOLLOW_RATE(190));
    return p;
}

/** How deep in a groove a relief coordinate is, 0..1. */
float hollowCut(float relief) {
    float c = abs(fract(relief) - 0.5);
    return smoothstep(0.1, 0.0, c);
}

vec2 hollowField(vec3 p) {
    vec3 q = hollowBend(p);
    vec2 m = hollowMap(q);
    vec3 s = sin(q * 4.0 + uTime * HOLLOW_RATE(1500) + vec3(0.0, 2.1, 4.2));
    m.x += gHollowChaos * 0.05 * (s.x * s.y + s.z);
    m.x += gHollowGroove * hollowCut(m.y);
    return m;
}

/** The eye, held where the air is. */
vec3 hollowEye(vec3 ro) {
    ro.z = mod(ro.z, HOLLOW_PERIOD);
    float r = length(ro.xy);
    if (r > gHollowBore) ro.xy *= gHollowBore / r;
    return ro;
}

float hollowDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** The light along one ray. */
vec3 hollowTrace(vec3 ro, vec3 rd) {
    float n = min(uSteps, float(HOLLOW_MAX_STEPS));
    float t = 0.02 + 0.05 * hollowDither();
    vec2 m = vec2(1.0, 0.0);
    bool hit = false;
    float near = 1.0e3;
    for (int i = 0; i < HOLLOW_MAX_STEPS; i++) {
        if (float(i) >= n || t > gHollowFar) break;
        m = hollowField(ro + rd * t);
        near = min(near, m.x / (0.4 + 0.25 * t));
        if (m.x < 0.0015 * (1.0 + t)) {
            hit = true;
            break;
        }
        t += m.x * gHollowStride;
    }
    t = min(t, gHollowFar);
    float depth = t / gHollowFar;
    float hue = gHollowTone + 0.4 * depth;
    vec3 col = vec3(0.0);
    if (hit) {
        vec3 p = ro + rd * t;
        vec2 e = vec2(1.0, -1.0) * 0.003 * (1.0 + t);
        vec3 nor = normalize(
            e.xyy * hollowField(p + e.xyy).x + e.yyx * hollowField(p + e.yyx).x +
            e.yxy * hollowField(p + e.yxy).x + e.xxx * hollowField(p + e.xxx).x
        );
        float ndv = max(dot(nor, -rd), 0.0);
        float rim = pow(1.0 - ndv, 3.0);
        float occ = clamp(hollowField(p + nor * 0.25).x / 0.25, 0.0, 1.0);
        float lamp = ndv / (1.0 + 0.1 * t * t);
        col = hollowCol(hue) * (0.03 + 0.55 * lamp) * (0.25 + 0.75 * occ);
        col += hollowCol(hue + 0.45) * rim * (0.15 + 0.75 * occ);
        float groove = hollowCut(m.y);
        col += hollowCol(hue + 0.25 + 0.381966 * floor(m.y)) * groove * gHollowGlow * 1.5 * (0.5 + 0.5 * occ);
        col += hollowCol(hue + 0.6) * pow(max(dot(reflect(rd, nor), normalize(vec3(0.3, 0.7, 0.6))), 0.0), 24.0) * 0.5;
    }
    float air = exp(-3.2 * depth);
    // Light skims every wall the ray passes close to, and the far end of the way is never quite dark.
    col = col * air + hollowCol(hue + 0.5) * (0.03 * (1.0 - air) + 0.02 * exp(-6.0 * max(near, 0.0)));
    return col;
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 hollowFinish(vec3 col, float exposure) {
    col *= exposure * 1.4 * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.25), 0.0);
    return vec4(grade(neonAces(col)), 1.0);
}
