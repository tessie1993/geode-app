// The Loom family: woven strands and stretched skins, taut threads, braids,
// nets and cloth strung across the dark; the camera rig flies through them or
// turns about them.
//
// WHAT IS DRAWN. Each style gives one map, loomMap(p): the distance to its
// nearest strand, that strand's fibre coordinate and palette position, and the
// distance to its nearest skin. A strand is solid thread: the ray is sphere
// traced to it and it is shaded once, lit from within along its fibres (a
// bright fibre at every whole number of the coordinate, so a yarn shows its
// plies and its twist), with a lamp at the eye, a rim on every edge that
// turns away and one cheap occlusion tap where threads cross. A skin is a
// membrane the ray goes through: up to LOOM_SKINS of them along one ray, each
// adding its soft light (more where it is seen along its surface) and dimming
// what lies behind. A thread the ray only passes close to leaves its glow, so
// a thread too fine or too far to hit still reads as a lit line. Far off, a
// strand's fibres blend into its mean light, so a fine weave does not shimmer.
//
// COST. One loop of LOOM_STEPS steps (72 at the default Detail, 90 at most),
// each one loomMap; a hit adds four for its normal and one for occlusion, so
// at most 95 loomMap per pixel. The maps are a few yarns found by repeating
// space, with no noise octaves and no texture. The palette is read five times
// per pixel before the loop (loomSetup) and mixed after.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gLoomForm    the weave's own metamorphosis, named by each style
//   Warp        gLoomFlow    loomBend(): the work twists about the way ahead
//                            and swells and draws in; the way itself stays open
//   Turbulence  gLoomChaos   a tremble through the threads
//   Intensity   neonGlow()   the emission
//   k0 PULSE the emission, k1 TEXTURE the fibres' glow, k2 FLOW the bend,
//   k3 TONE the palette position, k4 BUILD the skins' light and the threads'
//   glow in the air, k5 SPACE the reach.
//
// THE SOUND, heard the way the older families hear it (lib_dive, lib_neon):
// lib_sound's shaped reads, in loomSetup(), on the motion only, never on a
// colour, a brightness or a beat.
//
//   gLoomSwell  the low band, sndShaped(SND_LOW), deepens the Warp's swell,
//   never past the Warp's own reach.
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
// The world repeats along z every LOOM_PERIOD (16, dividing the rig's 8192),
// and every clock phase is whole cycles per uTime wrap (LOOM_RATE). The eye
// is held inside gLoomBore of the axis, where every flown style keeps open
// air.
//
// GROW AND LAYERS (lib_grow, loomField): Spawn slides a second weave off the
// first along the way, Fractal spins strands a third the size round every
// strand, Front layer opens the weave round the eye and Back layer the weave
// down the way, and billows the skins, the loom's own backdrop. Each costs
// nothing at 0.
//
// INCLUDE ORDER: after lib_scene_uniforms (with VIEW_OWNS_SHAPE),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon.
// The style then defines vec4 loomMap(vec3 p), its work at p (x the distance
// to the nearest strand, positive in the air; y that strand's fibre
// coordinate, a bright fibre at every whole number; z the distance to the
// nearest skin, either sign, 1.0e3 for none; w the strand's palette
// position), and after it includes lib_loom_march, which calls it.

#define LOOM_TAU 6.2831853
#define LOOM_PI 3.1415927
#define LOOM_MAX_STEPS 90
/** uSteps (64..128 by Detail, 102 at its default, less on a hot phone) on this loop, so Detail and the hot-phone cut reach it. */
#define LOOM_STEPS (min(uSteps * 0.7, float(LOOM_MAX_STEPS)))
#define LOOM_SKINS 3
#define LOOM_PERIOD 16.0
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define LOOM_RATE(k) (LOOM_TAU * float(k) / 7100.0)

float gLoomForm;
float gLoomFlow;
float gLoomChaos;
/** The low band's swell of the bend, 0..1.5 (see the header). */
float gLoomSwell;
/** How far the trace reaches, world units. */
float gLoomFar;
/** How brightly a strand's fibres glow, and how much light a skin and a thread's glow carry. */
float gLoomFibre;
float gLoomSkin;
float gLoomTone;
/** The share of a step taken: under 1 where a style's distance is only a bound. */
float gLoomStride;
/** How far from the axis the eye may ride. */
float gLoomBore;
vec3 gLoomPal0;
vec3 gLoomPal1;
vec3 gLoomPal2;
vec3 gLoomPal3;
vec3 gLoomPal4;

mat2 loomRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/** The angle of q, 0 where q has none (atan(0, 0) is undefined). */
float loomAngle(vec2 q) {
    return abs(q.x) + abs(q.y) > 1.0e-6 ? atan(q.y, q.x) : 0.0;
}

/** x in the nearest of the cells `period` apart, measured from that cell's middle. */
float loomRep(float x, float period) {
    return x - period * floor(x / period + 0.5);
}

float loomHash(vec2 c) {
    return fract(sin(dot(c, vec2(127.1, 311.7))) * 43758.5453);
}

/** The nearer of two strands (x distance, y fibre coordinate, z palette position). */
vec3 loomNearer(vec3 a, vec3 b) {
    return a.x < b.x ? a : b;
}

/**
 * A round yarn seen in cross-section, q the offset from its middle: `plies` fibres round it, laid
 * over by `along` (a coordinate that grows along the yarn, so the fibres wind).
 */
vec3 loomYarn(vec2 q, float radius, float plies, float along, float hue) {
    return vec3(length(q) - radius, plies * loomAngle(q) / LOOM_TAU + along, hue);
}

/** A flat tape in cross-section, q.x across its width: `plies` fibres side by side along it. */
vec3 loomTape(vec2 q, float wide, float plies, float hue) {
    float d = length(max(abs(q) - vec2(wide, 0.012), 0.0)) - 0.014;
    return vec3(d, 0.5 * plies * q.x / max(wide, 1.0e-3), hue);
}

/** A taut strand from a to b, its fibres winding once and a half from end to end. */
vec3 loomStrand(vec3 p, vec3 a, vec3 b, float radius, float hue) {
    vec3 ab = b - a;
    float h = clamp(dot(p - a, ab) / max(dot(ab, ab), 1.0e-6), 0.0, 1.0);
    vec3 off = p - a - ab * h;
    // The two axes most nearly across the strand.
    vec3 w = abs(ab);
    vec2 c = w.x > max(w.y, w.z) ? off.yz : (w.y > w.z ? off.zx : off.xy);
    return vec3(length(off) - radius, 2.0 * loomAngle(c) / LOOM_TAU + 1.5 * h, hue);
}

/** The user's palette at t, from the five reads of loomSetup(): there and back, so no seam. */
vec3 loomCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gLoomPal0, gLoomPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gLoomPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gLoomPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gLoomPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

void loomSetup() {
    gLoomForm = neonForm();
    gLoomFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gLoomChaos = neonChaos();
    gLoomFar = 14.0 * (1.0 + 0.4 * neonKnob(5));
    gLoomFibre = 1.0 + neonKnob(1);
    gLoomSkin = 1.0 + neonKnob(4);
    gLoomTone = 0.6 * neonKnob(3);
    gLoomStride = 0.7;
    gLoomBore = 0.3;
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gLoomSwell = sndShaped(SND_LOW) * drive;
    gLoomPal0 = pal(0.0);
    gLoomPal1 = pal(0.25);
    gLoomPal2 = pal(0.5);
    gLoomPal3 = pal(0.75);
    gLoomPal4 = pal(1.0);
}

/**
 * Warp and Turbulence on space, about the axis so the way ahead stays open: a twist that comes and
 * goes, a swell and a drawing in (and the lows'), a tremble that is nothing at
 * the axis.
 */
vec3 loomBend(vec3 p) {
    float zc = p.z * (LOOM_TAU / LOOM_PERIOD);
    float r = length(p.xy);
    p.xy = loomRot(gLoomFlow * 1.2 * sin(zc + 0.5 * r + uTime * LOOM_RATE(160))) * p.xy;
    p.xy *= 1.0 + 0.3 * min(gLoomFlow + 0.3 * gLoomSwell, 1.0) * sin(2.0 * zc + 1.5 * p.y + uTime * LOOM_RATE(220));
    p.xy += gLoomChaos * 0.07 * min(r, 1.0) * sin(p.yx * 3.1 + 3.0 * zc + uTime * LOOM_RATE(1800) + vec2(0.0, 2.1));
    return p;
}

/** How near a fibre coordinate is to a fibre, 0..1. */
float loomPly(float fibre) {
    float c = abs(fract(fibre + 0.5) - 0.5);
    return 1.0 - smoothstep(0.0, 0.22, c);
}

/** The eye, held where the air is. */
vec3 loomEye(vec3 ro) {
    ro.z = mod(ro.z, LOOM_PERIOD);
    float r = length(ro.xy);
    if (r > gLoomBore) ro.xy *= gLoomBore / r;
    // Grow and layers measure their depth from here (lib_grow).
    growEye(ro);
    return ro;
}

float loomDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 loomFinish(vec3 col, float exposure) {
    col *= exposure * 1.4 * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.25), 0.0);
    col = grade(neonAces(col));
    // No pixel leaves as a NaN or an infinity: the feedback pass would carry it for good.
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    return vec4(col, 1.0);
}
