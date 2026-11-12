// The Opal family: clear bodies with an oil-film skin, soap bubbles, shells and
// melting drops, hung in the dark; the camera rig flies through them or turns
// about them.
//
// WHAT IS DRAWN. Each style gives one signed distance, opalMap(p), and a skin
// coordinate beside it. The ray is sphere-traced to the nearest skin, shaded,
// and carried on through: up to OPAL_HITS skins along one ray, each dimmer
// than the last, so a body shows its far wall and the bodies behind it. A skin
// is a thin film: its colour runs through the palette with the angle it is
// seen at, so every body carries bands that crowd toward its rim, a bright rim
// line, one soft highlight, and (where a style asks) fine lines drawn along
// the skin coordinate. The steps spent near a body leave a faint halo.
//
// COST. One loop of OPAL_STEPS steps (80 at the default Detail, 100 at most),
// each one opalMap; a hit adds four more for its normal, so at most
// 100 + 4 * OPAL_HITS = 116 opalMap per pixel.
// The maps are smooth unions of a few spheres, tori or one lattice term; no
// noise octaves and no texture. The palette is read five times per pixel
// before the loop (opalSetup) and mixed after.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gOpalForm    the bodies' own metamorphosis, named by each style
//   Warp        gOpalFlow    opalBend(): space twists about the way ahead and
//                            swells sideways, so every body is pulled out of true
//   Turbulence  gOpalChaos   a ripple over every skin
//   Intensity   neonGlow()   the emission
//   k0 PULSE the emission, k1 TEXTURE the skin lines, k2 FLOW the bend,
//   k3 TONE the palette position, k4 BUILD the film's bands, k5 SPACE the reach.
//
// THE SOUND, heard the way the older families hear it (lib_dive, lib_neon):
// lib_sound's shaped reads, in opalSetup(), on the motion only, never on a
// colour, a brightness or a beat.
//
//   gOpalTurn   the mid band, sndShaped(SND_MIDDLE), pushes space
//   round about the way ahead while the mids climb and eases back as they
//   fall, as lib_dive's gDiveSpin does. Mid gain scales it.
//   gOpalSwell  the low band, sndShaped(SND_LOW), widens the Warp's sideways swell,
//   never past the Warp's own reach.
//
// Both are times SND_DRIVE, the music's absolute magnitude (lib_sound): how
// MUCH moves follows how loud and dense the music is, never the song's own
// average, and the band only shapes the push. sndShaped scales both by Audio
// drive (uSoundDrive). Like Dive and Glass, no style here has a spin rate
// (catalogue::spinRate, owner 2026-11-12), so Spin drift does not turn it.
//
// What moves on a hit is the reaction matrix, through the params above. uTime
// stops in silence and SND_DRIVE falls to 0, so everything here comes to rest.
//
// The world repeats along z every OPAL_PERIOD (16, dividing the rig's 8192),
// and every clock phase is whole cycles per uTime wrap (OPAL_RATE).
//
// INCLUDE ORDER: after lib_scene_uniforms (with VIEW_OWNS_SHAPE),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon.
// The style then defines vec2 opalMap(vec3 p), its bodies at p (x the signed
// distance to the nearest skin, y the skin coordinate, a line at every whole
// number), and after it includes lib_opal_march, which calls it.

#define OPAL_TAU 6.2831853
#define OPAL_MAX_STEPS 100
/** uSteps (64..128 by Detail, 102 at its default, less on a hot phone) on this loop: a cap at 80 left Detail and the hot-phone cut dead above it. */
#define OPAL_STEPS (min(uSteps * 0.78, float(OPAL_MAX_STEPS)))
#define OPAL_HITS 4
#define OPAL_PERIOD 16.0
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define OPAL_RATE(k) (OPAL_TAU * float(k) / 7100.0)

float gOpalForm;
float gOpalFlow;
float gOpalChaos;
/** The mid band's push on the twist about z, radians, and the low band's swell of the bend, 0..1.5 (see the header). */
float gOpalTurn;
float gOpalSwell;
/** How far the trace reaches, world units. */
float gOpalFar;
/** How many times the film's colour runs through the palette from a skin's middle to its rim. */
float gOpalBands;
/** How strongly the skin coordinate's lines are drawn, 0 for none. */
float gOpalLines;
float gOpalTone;
/** The share of a step taken: under 1 where a style's distance is only a bound. */
float gOpalStride;
vec3 gOpalPal0;
vec3 gOpalPal1;
vec3 gOpalPal2;
vec3 gOpalPal3;
vec3 gOpalPal4;

mat2 opalRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

float opalSmin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

float opalHash(vec2 c) {
    return fract(sin(dot(c, vec2(127.1, 311.7))) * 43758.5453);
}

/** The user's palette at t, from the five reads of opalSetup(): there and back, so no seam. */
vec3 opalCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gOpalPal0, gOpalPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gOpalPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gOpalPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gOpalPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

void opalSetup() {
    gOpalForm = neonForm();
    gOpalFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gOpalChaos = neonChaos();
    gOpalFar = 11.0 * (1.0 + 0.4 * neonKnob(5));
    gOpalBands = 1.6 * (1.0 + 0.8 * neonKnob(4));
    gOpalLines = 0.6 * (1.0 + neonKnob(1));
    gOpalTone = 0.6 * neonKnob(3);
    gOpalStride = 0.85;
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gOpalTurn = 0.6 * sndShaped(SND_MIDDLE) * drive;
    gOpalSwell = sndShaped(SND_LOW) * drive;
    gOpalPal0 = pal(0.0);
    gOpalPal1 = pal(0.25);
    gOpalPal2 = pal(0.5);
    gOpalPal3 = pal(0.75);
    gOpalPal4 = pal(1.0);
}

/** Warp on space: a twist about z that comes and goes along the way (and the mids' push), and a sideways swell (and the lows'). */
vec3 opalBend(vec3 p) {
    float zc = p.z * (OPAL_TAU / OPAL_PERIOD);
    p.xy = opalRot(gOpalFlow * 0.9 * sin(zc + 0.6 * p.y + uTime * OPAL_RATE(150)) + gOpalTurn) * p.xy;
    p.x += 0.5 * min(gOpalFlow + 0.3 * gOpalSwell, 1.0) * sin(2.0 * zc + 1.1 * p.y + uTime * OPAL_RATE(210));
    p.y += gOpalFlow * 0.4 * sin(zc + 1.3 * p.x);
    return p;
}

float opalDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 opalFinish(vec3 col, float exposure) {
    col *= exposure * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    return vec4(grade(neonAces(col)), 1.0);
}
