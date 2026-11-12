// The Fold family: fractals made by folding space (mirrors, box and ball
// folds, inversions, the quaternion and bicomplex squares) and iterating,
// each drawn by a march built for that kind of fractal; the camera rig flies
// through them or turns about them.
//
// WHAT IS DRAWN. Each style gives one map, foldMap(p): a vec4 holding
//   x  how far p is outside the fractal: a distance bound in world units for
//      the glow, cone, slice and pair marches, negative inside where the
//      style can tell; for the probe march any field that is negative inside
//      and about a world unit per unit near its surface,
//   y  the orbit's palette position,
//   zw two orbit traps: how near, in world units, the orbit came to two trap
//      sets (a line for the glow march, a plane for the others), each the
//      least over the first few iterations, so a trap is continuous.
// The style then includes ONE of five marches, each a different way to turn
// that map into a picture:
//
//   lib_fold_march_glow   no surface at all: the traps' light gathered along
//                         the ray, so the fractal is a web of luminous threads
//   lib_fold_march_cone   the surface, sphere traced; the hit distance widens
//                         with the distance from the eye, as a pixel does
//   lib_fold_march_slice  the ray met with a stack of planes or nested
//                         spheres; each cuts a four-dimensional fractal in a
//                         thin translucent section, laid over the next front
//                         to back
//   lib_fold_march_pair   a surface whose distance is only a local bound (two
//                         fold rules taking turns): short capped steps, and a
//                         step back whenever one lands inside
//   lib_fold_march_probe  a surface with no distance at all: strides until
//                         the field changes sign, then the crossing halved in on
//
// COST. One loop per march, bounded by the Detail budget (the FOLD_*_STEPS
// below), each pass one foldMap of at most 7 fold iterations (10 for the
// slices' four-dimensional squares): at most 64 glow samples x 7, 60 cone or
// pair steps x 7 plus 5 for the hit, 40 sections x 10, 52 probe strides x 7
// plus 10 for the hit. The heaviest, 65 x 7 = 455, stays under Hollow Roots'
// 95 x 5 at the same Detail. No noise octaves and no texture. The palette is
// read five times per pixel before the loop (foldSetup) and mixed after.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gFoldForm    the fractal's own metamorphosis, named by each style
//   Warp        gFoldFlow    foldBend(): space twists about the way ahead and
//                            sways sideways (a corridor swells and pinches
//                            about its axis instead, so the way stays open)
//   Turbulence  gFoldChaos   a restless ripple through the whole fractal
//   Intensity   neonGlow()   the emission
//   k0 PULSE the emission, k1 TEXTURE the fineness (thread, seam and outline
//   width, the surface's precision), k2 FLOW the bend, k3 TONE the palette
//   position, k4 BUILD the seams' and the threads' glow, k5 SPACE the reach.
//
// THE SOUND, heard the way the older families hear it (lib_dive, lib_neon):
// lib_sound's shaped reads, in foldSetup(), on the motion only, never on a
// colour, a brightness or a beat.
//
//   gFoldSwell  the low band, sndShaped(SND_LOW), widens the Warp's sway (or a
//   corridor's swell), never past the Warp's own reach.
//
// It is times SND_DRIVE, the music's absolute magnitude (lib_sound): how
// MUCH moves follows how loud and dense the music is, never the song's own
// average, and the band only shapes the push. sndShaped scales it by Audio
// drive (uSoundDrive). Like Dive and Glass, no style here has a spin rate
// (catalogue::spinRate, owner 2026-11-12), so Spin drift does not turn it.
// No sound band turns it either: the mids' push round the way ahead is gone
// (owner 2026-11-12, no style spins to the music).
//
// What moves on a hit is the reaction matrix, through the params above. uTime
// stops in silence and SND_DRIVE falls to 0, so everything here comes to rest.
//
// The world repeats along z every FOLD_PERIOD (16, dividing the rig's 8192),
// and every clock phase is whole cycles per uTime wrap (FOLD_RATE).
//
// SAFE MATHS. Every inversion clamps its radius from below, every running
// scale is clamped, no root or log sees a negative number or zero, and every
// iterated fold keeps a fixed point, so its fractal is never empty.
//
// INCLUDE ORDER: after lib_scene_uniforms (with VIEW_OWNS_SHAPE),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon.
// The style then defines vec4 foldMap(vec3 p) and after it includes one
// lib_fold_march_<kind>, which calls it.

#define FOLD_TAU 6.2831853
#define FOLD_PERIOD 16.0
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define FOLD_RATE(k) (FOLD_TAU * float(k) / 7100.0)
// uSteps (64..128 by Detail, 102 at its default, less on a hot phone) on each march's loop.
#define FOLD_GLOW_MAX 64
#define FOLD_GLOW_STEPS (min(uSteps * 0.5, float(FOLD_GLOW_MAX)))
#define FOLD_CONE_MAX 60
#define FOLD_CONE_STEPS (min(uSteps * 0.47, float(FOLD_CONE_MAX)))
#define FOLD_PAIR_MAX 60
#define FOLD_PAIR_STEPS (min(uSteps * 0.47, float(FOLD_PAIR_MAX)))
#define FOLD_SLICE_MAX 40
#define FOLD_SLICE_STEPS (min(uSteps * 0.31, float(FOLD_SLICE_MAX)))
#define FOLD_PROBE_MAX 52
#define FOLD_PROBE_STEPS (min(uSteps * 0.41, float(FOLD_PROBE_MAX)))

float gFoldForm;
float gFoldFlow;
float gFoldChaos;
/** The low band's swell of the bend, 0..1.5 (see the header). */
float gFoldSwell;
/** How far a march reaches, world units. */
float gFoldFar;
/** A thread's, a seam's or an outline's half-width at one unit from the eye, world units. */
float gFoldFine;
/** How brightly the seams and the threads glow. */
float gFoldBuild;
float gFoldTone;
/** The share of a distance taken as a step: under 1 where a style's distance is only a bound. */
float gFoldStride;
/** The stride's cut while Warp and Turbulence stretch space, 0..1. */
float gFoldSlack;
/** The hit distance per unit of distance from the eye: about a pixel and a half. */
float gFoldCone;
/** 1 for a corridor: the bend keeps to the axis, so the way ahead stays open. */
float gFoldAxial;
/** How far from the axis a corridor's eye may ride. */
float gFoldBore;
/** The glow march: how far outside the fractal (foldMap's x) its threads fade out, world units. */
float gFoldEdge;
/** The seams' strength on a surface or a section, and a surface's gloss. */
float gFoldSeam;
float gFoldGloss;
/** The pair march: the longest step, world units. */
float gFoldCap;
/** The slice march: the gap between cut planes, the outermost shell's radius and the number of shells, a section's opacity. */
float gFoldGap;
float gFoldShellR;
float gFoldShells;
float gFoldDense;
vec3 gFoldPal0;
vec3 gFoldPal1;
vec3 gFoldPal2;
vec3 gFoldPal3;
vec3 gFoldPal4;

mat2 foldRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/** q turned about the unit vector `axis` by the angle whose cosine and sine are c and s: `axis` itself stays put. */
vec3 foldTwist(vec3 q, vec3 axis, float c, float s) {
    return q * c + cross(axis, q) * s + axis * (dot(axis, q) * (1.0 - c));
}

/** Mirror fold: whatever lies behind the plane through the origin with unit normal n is reflected to its front. */
vec3 foldMirror(vec3 q, vec3 n) {
    return q - 2.0 * min(dot(q, n), 0.0) * n;
}

/** Box fold: each coordinate past +-l is reflected back in. */
vec3 foldBox(vec3 q, float l) {
    return clamp(q, -l, l) * 2.0 - q;
}

/** Ball fold: the factor that inverts q in the unit sphere, 1 outside it and at most 1 / inner near the centre. */
float foldBall(vec3 q, float inner) {
    return clamp(1.0 / max(dot(q, q), 1.0e-6), 1.0, 1.0 / inner);
}

/** Octahedral fold: every sign and every order of the coordinates to one wedge, x >= y >= z >= 0. */
vec3 foldOcta(vec3 q) {
    q = abs(q);
    if (q.x < q.y) q.xy = q.yx;
    if (q.x < q.z) q.xz = q.zx;
    if (q.y < q.z) q.yz = q.zy;
    return q;
}

/** The quaternion square. */
vec4 foldQuatSq(vec4 q) {
    return vec4(q.x * q.x - dot(q.yzw, q.yzw), 2.0 * q.x * q.yzw);
}

/** Mirror repetition with period 2 h: a triangle wave from -h/2 to h/2, so neighbouring cells meet without a seam. */
vec3 foldRepeat(vec3 p, float h) {
    return abs(mod(p + h, 2.0 * h) - h) - 0.5 * h;
}

/** The user's palette at t, from the five reads of foldSetup(): there and back, so no seam. */
vec3 foldCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gFoldPal0, gFoldPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gFoldPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gFoldPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gFoldPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

void foldSetup() {
    gFoldForm = neonForm();
    gFoldFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gFoldChaos = neonChaos();
    gFoldFar = 11.0 * (1.0 + 0.4 * neonKnob(5));
    gFoldFine = 0.008 * (1.0 - neonKnob(1));
    gFoldBuild = 1.0 + neonKnob(4);
    gFoldTone = 0.6 * neonKnob(3);
    gFoldStride = 0.8;
    gFoldSlack = 1.0 / (1.0 + 0.9 * gFoldFlow + 0.3 * gFoldChaos);
    gFoldCone = clamp(3.0 / (max(uResolution.y, 1.0) * max(uCamFocal, 0.1)), 0.0012, 0.004) * (1.0 - 0.6 * neonKnob(1));
    gFoldAxial = 0.0;
    gFoldBore = 0.3;
    gFoldEdge = 0.35;
    gFoldSeam = 1.0;
    gFoldGloss = 0.5;
    gFoldCap = 0.2;
    gFoldGap = 0.25;
    gFoldShellR = 1.5;
    gFoldShells = 16.0;
    gFoldDense = 0.5;
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gFoldSwell = sndShaped(SND_LOW) * drive;
    gFoldPal0 = pal(0.0);
    gFoldPal1 = pal(0.25);
    gFoldPal2 = pal(0.5);
    gFoldPal3 = pal(0.75);
    gFoldPal4 = pal(1.0);
}

/**
 * Warp and Turbulence on space: a twist about z that comes and goes along the way,
 * a sideways sway (and the lows' swell), or for a corridor a swell and pinch about the axis, and a ripple.
 */
vec3 foldBend(vec3 p) {
    float zc = p.z * (FOLD_TAU / FOLD_PERIOD);
    float reach = min(gFoldFlow + 0.3 * gFoldSwell, 1.0);
    float side = 1.0 - gFoldAxial;
    p.xy = foldRot(gFoldFlow * sin(zc + uTime * FOLD_RATE(160))) * p.xy;
    p.xy *= 1.0 + gFoldAxial * 0.3 * reach * sin(3.0 * zc + uTime * FOLD_RATE(200));
    p.x += side * 0.5 * reach * sin(2.0 * zc + 0.9 * p.y + uTime * FOLD_RATE(230));
    p.y += side * gFoldFlow * 0.4 * cos(zc - 0.7 * p.x);
    p += gFoldChaos * 0.07 * sin(p.yzx * 3.1415927 + uTime * FOLD_RATE(1800) + vec3(0.0, 2.1, 4.2));
    return p;
}

/** A corridor's eye, held where the air is. */
vec3 foldEye(vec3 ro) {
    ro.z = mod(ro.z, FOLD_PERIOD);
    float r = length(ro.xy);
    if (r > gFoldBore) ro.xy *= gFoldBore / r;
    return ro;
}

float foldDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** A thread's, a seam's or an outline's half-width at t from the eye: wider with distance, so a far one gathers into glow instead of noise. */
float foldWidth(float t) {
    return gFoldFine * (0.35 + 0.65 * t);
}

/** How brightly the two trap seams show at trap distances v, for a seam of half-width w: 1 on a seam. */
float foldSeams(vec2 v, float w) {
    vec2 s = w * w / (v * v + w * w);
    return s.x + 0.4 * s.y;
}

/**
 * A surface point lit: a lamp at the eye that falls off with distance, one key light, a rim on every edge
 * that turns away, the glowing seams, a gloss. `nor` is unit, m is foldMap there, occ its occlusion 0..1.
 */
vec3 foldLit(vec3 nor, vec3 rd, float t, vec4 m, float occ) {
    vec3 key = normalize(vec3(0.45, 0.8, -0.35));
    float ndv = clamp(dot(nor, -rd), 0.0, 1.0);
    float rim = (1.0 - ndv) * (1.0 - ndv) * (1.0 - ndv);
    float hue = gFoldTone + m.y + 0.3 * t / gFoldFar;
    float lamp = ndv / (1.0 + 0.08 * t * t);
    float sun = clamp(dot(nor, key), 0.0, 1.0);
    vec3 col = foldCol(hue) * (0.04 + 0.5 * lamp + 0.4 * sun) * (0.25 + 0.75 * occ);
    col += foldCol(hue + 0.45) * rim * (0.15 + 0.7 * occ);
    col += foldCol(hue + 0.25) * foldSeams(m.zw, foldWidth(t)) * gFoldBuild * gFoldSeam * (0.5 + 0.5 * occ);
    float gloss = clamp(dot(reflect(rd, nor), key), 0.0, 1.0);
    col += foldCol(hue + 0.6) * pow(gloss, 24.0) * gFoldGloss;
    return col;
}

/** The air over a surface colour at t: far things fade into the palette's far end; `near` is how near, over distance, the ray passed a surface. */
vec3 foldAir(vec3 col, float t, float near) {
    float depth = clamp(t / gFoldFar, 0.0, 1.0);
    float air = exp(-3.0 * depth);
    float skim = exp(-6.0 * max(near, 0.0));
    return col * air + foldCol(gFoldTone + 0.4 * depth + 0.5) * (0.03 * (1.0 - air) + 0.03 * skim);
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 foldFinish(vec3 col, float exposure) {
    col *= exposure * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    col = grade(neonAces(col));
    // No pixel leaves as a NaN or an infinity: the feedback pass would carry it for good.
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    return vec4(col, 1.0);
}

// The depth layers round the style's form and its formulas (uFormula): see lib_layers.
//#include lib_layers
