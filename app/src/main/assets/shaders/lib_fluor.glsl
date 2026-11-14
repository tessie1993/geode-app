// The Fluor family: carved and embossed ornament under blacklight, flown
// along or turned about: whorls, lattice, coffers, weave, scales, filigree;
// dark relief whose raised edges glow like fluorescent paint on black.
//
// WHAT IS DRAWN. Each style gives one distance to its relief, fluorMap(p),
// positive in the open air, and a paint position beside it. The relief is
// built of real raised strips, bosses and sunk panels (fluorStrip, fluorSlab,
// fluorBox), not of lines drawn on a wall. The ray is sphere-traced to the
// relief and shaded once. The four taps that give the normal also give how
// sharply the relief bends there (fluorShape): every raised edge, wherever it
// runs and at whatever angle it is seen, bends outward and takes the paint,
// which glows by itself; every crease and fillet at the foot of a ridge bends
// inward and falls dark. Between them the stone is lit faintly from the eye
// and shaded by one cheap occlusion tap, and a depth haze runs through the
// palette with distance. So the paint follows the carving exactly, where
// lib_hollow cuts grooves at whole numbers of a coordinate.
//
// COST. One loop of FLUOR_STEPS steps (72 at the default Detail, 90 at most),
// each one fluorMap; the hit adds four for its normal and bend and one for
// occlusion, so at most 95 fluorMap per pixel. The maps are strips, boxes and
// smooth unions over a cell or two; no noise octaves and no texture. The
// palette is read five times per pixel before the loop (fluorSetup) and mixed
// after.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gFluorForm   the relief's own metamorphosis, named by each style
//   Warp        gFluorFlow   fluorBend(): the carving twists about the way
//                            ahead and swells and sinks; the way stays open
//   Turbulence  gFluorChaos  a ripple over the stone
//   Intensity   neonGlow()   the emission
//   k0 PULSE the emission, k1 TEXTURE the paint line's width, k2 FLOW the bend,
//   k3 TONE the palette position, k4 BUILD the paint's glow, k5 SPACE the reach.
//
// GROW AND LAYERS (lib_grow, fluorField): Spawn slides a second carving off
// the first along the way, Fractal carves the relief again a third the size
// on its raised faces, Front layer opens the carving round the eye and Back
// layer the carving down the way. Each costs nothing at 0.
//
// THE SOUND, heard the way the older families hear it (lib_dive, lib_neon):
// lib_sound's shaped reads, in fluorSetup(), on the motion only, never on a
// colour, a brightness or a beat.
//
//   gFluorSwell  the low band, sndShaped(SND_LOW), deepens the Warp's swell
//   and sink, never past the Warp's own reach.
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
// The world repeats along z every FLUOR_PERIOD (16, dividing the rig's 8192):
// every cell, row and paint index along z divides it. Every clock phase is
// whole cycles per uTime wrap (FLUOR_RATE). A flying eye is held inside
// gFluorBore of the axis, where every style keeps open air.
//
// INCLUDE ORDER: after lib_scene_uniforms (with VIEW_OWNS_SHAPE),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon.
// The style then defines vec2 fluorMap(vec3 p), its relief at p (x the
// distance to it, positive in the air, y the paint's palette position), and
// after it includes lib_fluor_march, which calls it.

#define FLUOR_TAU 6.2831853
#define FLUOR_MAX_STEPS 90
/** uSteps (64..128 by Detail, 102 at its default) on this loop, as in lib_hollow. */
#define FLUOR_STEPS (min(uSteps * 0.7, float(FLUOR_MAX_STEPS)))
#define FLUOR_PERIOD 16.0
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define FLUOR_RATE(k) (FLUOR_TAU * float(k) / 7100.0)

float gFluorForm;
float gFluorFlow;
float gFluorChaos;
/** The low band's swell of the bend, 0..1.5 (see the header). */
float gFluorSwell;
/** How far the trace reaches, world units. */
float gFluorFar;
/** The paint line's width near the eye, world units: the bend is read this far either side. */
float gFluorLine;
/** How brightly the paint glows. */
float gFluorGlow;
float gFluorTone;
/** The share of a step taken: under 1 where a style's distance is only a bound. */
float gFluorStride;
/** How far from the axis the eye may ride. */
float gFluorBore;
vec3 gFluorPal0;
vec3 gFluorPal1;
vec3 gFluorPal2;
vec3 gFluorPal3;
vec3 gFluorPal4;

mat2 fluorRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/** Smooth union: the smaller distance, filleted where the two meet. */
float fluorSmin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

/** The angle of q about the origin, never atan(0, 0). */
float fluorAngle(vec2 q) {
    return atan(q.y, q.x + (q.x < 0.0 ? -1.0e-6 : 1.0e-6));
}

/** A box of half-size b about the origin. */
float fluorBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

/** A flat shape, its distance d2 in its own plane, given half-thickness `thick` across it at z. */
float fluorSlab(float d2, float z, float thick) {
    vec2 w = vec2(d2, abs(z) - thick);
    return min(max(w.x, w.y), 0.0) + length(max(w, 0.0));
}

/**
 * A strip raised on the stone: h the height above the stone (positive in the
 * air), d the distance across from the strip's middle line, w its half-width,
 * top its height. Its edges are rounded a little, so they bend sharply but
 * never fold.
 */
float fluorStrip(float h, float d, float w, float top) {
    float r = 0.2 * min(w, top);
    vec2 q = vec2(abs(d) - w + r, h - top + r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

/** The user's palette at t, from the five reads of fluorSetup(): there and back, so no seam. */
vec3 fluorCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gFluorPal0, gFluorPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gFluorPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gFluorPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gFluorPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

void fluorSetup() {
    gFluorForm = neonForm();
    gFluorFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gFluorChaos = neonChaos();
    gFluorFar = 12.0 * (1.0 + 0.4 * neonKnob(5));
    gFluorLine = 0.008 * (1.0 + neonKnob(1));
    gFluorGlow = 1.0 + neonKnob(4);
    gFluorTone = 0.6 * neonKnob(3);
    gFluorStride = 0.7;
    gFluorBore = 0.3;
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gFluorSwell = sndShaped(SND_LOW) * drive;
    gFluorPal0 = pal(0.0);
    gFluorPal1 = pal(0.25);
    gFluorPal2 = pal(0.5);
    gFluorPal3 = pal(0.75);
    gFluorPal4 = pal(1.0);
}

/** Warp on space, about the axis so the way ahead stays open: a twist that comes and goes, and a swell and sink (and the lows'). */
vec3 fluorBend(vec3 p) {
    float zc = p.z * (FLUOR_TAU / FLUOR_PERIOD);
    float r = length(p.xy);
    p.xy = fluorRot(gFluorFlow * 1.1 * sin(zc + 0.5 * r + uTime * FLUOR_RATE(130))) * p.xy;
    p.xy *= 1.0 + 0.3 * min(gFluorFlow + 0.3 * gFluorSwell, 1.0) * sin(2.0 * zc + 1.5 * p.y + uTime * FLUOR_RATE(170));
    return p;
}

/** The eye, held where the air is. */
vec3 fluorEye(vec3 ro) {
    ro.z = mod(ro.z, FLUOR_PERIOD);
    float r = length(ro.xy);
    if (r > gFluorBore) ro.xy *= gFluorBore / r;
    // Grow and layers measure their depth from here (lib_grow).
    growEye(ro);
    return ro;
}

float fluorDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 fluorFinish(vec3 col, float exposure) {
    col *= exposure * 1.4 * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.25), 0.0);
    col = grade(neonAces(col));
    // No pixel leaves as a NaN or an infinity: the feedback pass would carry it for good.
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    return vec4(col, 1.0);
}

/**
 * A fractal ornament raised on the stone: u the place on the stone, h the height above it. u is
 * folded gStrataIters times by lib_strata's fold `kind` (turned `turn` a fold) in units of `span`
 * world units, and a raised frame runs along every outline the folds make, `top` high at the
 * first scale and narrower and lower at each finer one: rings for kind 2 (the Apollonian),
 * squares for the others, so a frame is nested in frames at every scale. x the distance, y a
 * paint step for each scale. Needs lib_strata before this library.
 */
vec2 fluorOrnament(vec2 u, float h, float kind, vec2 c, float turn, float span, float top) {
    float dr = 1.0 / span;
    vec2 f = strataFold2(u * dr, kind, gStrataIters, c, fluorRot(turn), dr);
    float outline = kind > 1.5 && kind < 2.5 ? abs(length(f) - 0.8) : abs(max(abs(f.x), abs(f.y)) - 0.6);
    // A finer frame is narrower and lower, never under 0.01 wide, so its paint does not shimmer.
    float fine = inversesqrt(max(dr * span, 1.0));
    return vec2(fluorStrip(h, outline / dr, max(0.03 * fine, 0.01), top * fine), 0.381966 * floor(log2(max(dr * span, 1.0))));
}
