// The Prism family: kaleidoscope portals in liquid chrome. Space folded by
// mirrors into stars, rosettes, arches and fans, its surface a mirror whose
// tint runs through the palette, a thin ribbon of light along every fold.
//
// WHAT IS DRAWN. Each style folds space with prismFold (a ring of mirrors
// about an axis) or plain mirror planes, and gives one distance to its solid,
// prismMap(p), positive in the open air, with a ribbon distance beside it: how
// far p is from the nearest line of light, world units, so a style draws its
// lines where its folds meet the solid. The ray is sphere-traced to the solid
// and shaded as a mirror: what the surface sees along its reflection is a dark
// sky crossed by thin bright bands (prismSky), or the solid itself, found by
// one more short trace along the reflection. The mirror's tint runs through
// the palette as the surface turns from the eye, a pulse runs along the
// ribbons, and they glow into the air the ray passes. Between the lines the
// place is dark.
//
// COST. One loop of PRISM_STEPS steps (63 at the default Detail, 80 at most)
// and, on a hit, one of PRISM_ECHO (16, 20 at most), each one prismMap; the
// two hits add four each for their normals, so at most 108 prismMap per
// pixel. The maps are folds, hoops, plates and planes, at most two fold
// iterations; no noise octaves and no texture. The palette is read five
// times per pixel before the loop (prismSetup) and mixed after.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gPrismForm   the figure's own metamorphosis, named by each style
//   Warp        gPrismFlow   prismBend(): space twists about the way ahead and
//                            swells and pinches, the mirrors with it
//   Turbulence  gPrismChaos  a ripple over the chrome
//   Intensity   neonGlow()   the emission
//   k0 PULSE the emission, k1 TEXTURE how far the tint runs with the angle,
//   k2 FLOW the bend, k3 TONE the palette position, k4 BUILD the ribbons'
//   glow, k5 SPACE the reach.
//
// THE SOUND, heard the way the older families hear it (lib_dive, lib_neon):
// lib_sound's shaped reads, in prismSetup(), on the motion only, never on a
// colour, a brightness or a beat.
//
//   gPrismSwell  the low band, sndShaped(SND_LOW), deepens the Warp's swell
//   and pinch, never past the Warp's own reach.
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
// The world repeats along z every PRISM_PERIOD (16, dividing the rig's 8192),
// and every clock phase is whole cycles per uTime wrap (PRISM_RATE). A flown
// style's eye is held inside gPrismBore of the axis, where it keeps open air.
//
// INCLUDE ORDER: after lib_scene_uniforms (with VIEW_OWNS_SHAPE),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon.
// The style then defines vec2 prismMap(vec3 p), its solid at p (x the
// distance to it, positive in the air, y the distance to the nearest ribbon
// of light), and after it includes lib_prism_march, which calls it.

#define PRISM_TAU 6.2831853
#define PRISM_PI 3.14159265
#define PRISM_MAX_STEPS 80
/** uSteps (64..128 by Detail, 102 at its default, less on a hot phone) on both loops, so Detail and the hot-phone cut reach all of the march. */
#define PRISM_STEPS (min(uSteps * 0.62, float(PRISM_MAX_STEPS)))
#define PRISM_MAX_ECHO 20
#define PRISM_ECHO (min(uSteps * 0.16, float(PRISM_MAX_ECHO)))
#define PRISM_PERIOD 16.0
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define PRISM_RATE(k) (PRISM_TAU * float(k) / 7100.0)

float gPrismForm;
float gPrismFlow;
float gPrismChaos;
/** The low band's swell of the bend, 0..1.5 (see the header). */
float gPrismSwell;
/** How far the trace reaches, world units. */
float gPrismFar;
/** How far the mirror's tint runs through the palette from face-on to edge-on, and how brightly the ribbons glow. */
float gPrismSheen;
float gPrismLines;
float gPrismTone;
/** The share of a step taken: under 1 where a style's distance is only a bound. */
float gPrismStride;
/** How far from the axis the eye may ride. */
float gPrismBore;
vec3 gPrismPal0;
vec3 gPrismPal1;
vec3 gPrismPal2;
vec3 gPrismPal3;
vec3 gPrismPal4;

mat2 prismRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

float prismHash(vec3 c) {
    return fract(sin(dot(c, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

/** The angle of v, 0 where v has no length. */
float prismAngle(vec2 v) {
    return dot(v, v) > 1.0e-12 ? atan(v.y, v.x) : 0.0;
}

/**
 * n mirrors through the origin, PRISM_PI / n apart (n a constant, 2 or
 * more): v comes back in the half wedge between the mirror along +x and the
 * next one round, so x is out along the first mirror and y, never negative,
 * is the distance from it.
 */
vec2 prismFold(vec2 v, float n) {
    float w = PRISM_TAU / n;
    float a = prismAngle(v);
    a = abs(a - w * floor(a / w + 0.5));
    return length(v) * vec2(cos(a), sin(a));
}

/** How far a folded point is from the half wedge's other mirror. Never negative. */
float prismEdge(vec2 q, float n) {
    float h = PRISM_PI / n;
    return max(q.x * sin(h) - q.y * cos(h), 0.0);
}

/** The distance from v to the segment a..b. */
float prismSeg(vec2 v, vec2 a, vec2 b) {
    vec2 va = v - a;
    vec2 ba = b - a;
    float h = clamp(dot(va, ba) / max(dot(ba, ba), 1.0e-6), 0.0, 1.0);
    return length(va - ba * h);
}

/** A box's signed distance in 2D, `size` its half sizes. */
float prismBox(vec2 v, vec2 size) {
    vec2 b = abs(v) - size;
    return length(max(b, 0.0)) + min(max(b.x, b.y), 0.0);
}

/** The user's palette at t, from the five reads of prismSetup(): there and back, so no seam. */
vec3 prismCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gPrismPal0, gPrismPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gPrismPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gPrismPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gPrismPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

void prismSetup() {
    gPrismForm = neonForm();
    gPrismFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gPrismChaos = neonChaos();
    gPrismFar = 14.0 * (1.0 + 0.4 * neonKnob(5));
    gPrismSheen = 0.9 * (1.0 + neonKnob(1));
    gPrismLines = 1.0 + neonKnob(4);
    gPrismTone = 0.6 * neonKnob(3);
    gPrismStride = 0.7;
    gPrismBore = 0.3;
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gPrismSwell = sndShaped(SND_LOW) * drive;
    gPrismPal0 = pal(0.0);
    gPrismPal1 = pal(0.25);
    gPrismPal2 = pal(0.5);
    gPrismPal3 = pal(0.75);
    gPrismPal4 = pal(1.0);
}

/** Warp on space, about the axis so the way ahead stays open: a twist that comes and goes, and a swell and pinch (and the lows'). */
vec3 prismBend(vec3 p) {
    float zc = p.z * (PRISM_TAU / PRISM_PERIOD);
    p.xy = prismRot(gPrismFlow * 1.1 * sin(zc + uTime * PRISM_RATE(130))) * p.xy;
    p.xy *= 1.0 + 0.3 * min(gPrismFlow + 0.3 * gPrismSwell, 1.0) * sin(2.0 * zc + uTime * PRISM_RATE(180));
    return p;
}

/** How brightly a ribbon is lit at `away` world units from its line, 0..1; `w` its half-width. */
float prismLine(float away, float w) {
    return w * w / (away * away + w * w);
}

/** What a mirror sees where it sees no solid: dark, crossed by thin bright bands in the palette. */
vec3 prismSky(vec3 d, float hue) {
    float band = sin(5.0 * d.y + 2.0 * sin(3.0 * d.x + uTime * PRISM_RATE(260)) + 1.5 * d.z);
    band *= band;
    band *= band;
    return prismCol(hue + 0.35 * d.y + 0.2 * d.x) * (0.04 + 0.96 * band * band);
}

/** The eye, held where the air is. */
vec3 prismEye(vec3 ro) {
    ro.z = mod(ro.z, PRISM_PERIOD);
    float r = length(ro.xy);
    if (r > gPrismBore) ro.xy *= gPrismBore / r;
    return ro;
}

float prismDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 prismFinish(vec3 col, float exposure) {
    col *= exposure * 1.4 * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.25), 0.0);
    col = grade(neonAces(col));
    // No pixel leaves as a NaN or an infinity: the feedback pass would carry it for good.
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    return vec4(col, 1.0);
}
