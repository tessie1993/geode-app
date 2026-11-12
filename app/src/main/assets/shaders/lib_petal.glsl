// The Petal family: soft translucent sheets curled about a centre, petals,
// cups, scales and fronds, lit from inside; the camera rig flies through them
// or turns about them.
//
// WHAT IS DRAWN. Each style gives one distance, petalMap(p), to the middle of
// its thin sheets, signed by side, and two petal coordinates beside it: how
// far out toward its edge a point of a sheet lies (0 its heart, 1 its rim) and
// a vein coordinate, a vein at every whole number. The library gives every
// sheet the same small thickness. The ray is sphere-traced to each sheet in
// turn and carried on through it: up to PETAL_HITS sheets along one ray, each
// letting a share of the light behind it through. One lamp sits at the heart
// of the bloom (or a row of them along the way). A sheet with the lamp behind
// it glows with the light that comes through, most where it is seen square on;
// a sheet seen along its surface gathers light at its silhouette, and every
// sheet's own rim gathers light too. The lamp's glow is added along the ray
// from its exact integral, dimmed by every sheet in front of it, so the bloom
// is lit from inside through its petals. The steps spent near a sheet leave a
// faint halo.
//
// COST. One loop of PETAL_STEPS steps (76 at the default Detail, 96 at most),
// each one petalMap; a hit adds four more for its normal, so at most
// 96 + 4 * PETAL_HITS = 112 petalMap per pixel, and no map weighs more than
// three candidate sheets. The lamp's glow is two arctangents per lamp, once per sheet
// and once at the end. No noise octaves and no texture. The palette is read
// five times per pixel before the loop (petalSetup) and mixed after.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gPetalForm   the bloom's own opening, named by each style
//   Warp        gPetalFlow   petalBend(): a breeze; space curls about the way
//                            ahead and the sheets sway
//   Turbulence  gPetalChaos  a flutter over every sheet
//   Intensity   neonGlow()   the emission
//   k0 PULSE the emission, k1 TEXTURE the veins, k2 FLOW the bend,
//   k3 TONE the palette position, k4 BUILD how much light each sheet lets through, k5 SPACE the reach.
//
// THE SOUND, heard the way the older families hear it (lib_dive, lib_neon):
// lib_sound's shaped reads, in petalSetup(), on the motion only, never on a
// colour, a brightness or a beat.
//
//   gPetalSwell  the low band, sndShaped(SND_LOW), widens the Warp's sway of
//   the sheets, never past the Warp's own reach.
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
// The world repeats along z every PETAL_PERIOD (16, dividing the rig's 8192),
// and every clock phase is whole cycles per uTime wrap (PETAL_RATE).
//
// INCLUDE ORDER: after lib_scene_uniforms (with VIEW_OWNS_SHAPE),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon.
// The style then defines vec3 petalMap(vec3 p), its sheets at p (x the signed
// distance to the nearest sheet's middle, y how far toward that sheet's rim,
// z its vein coordinate), and after it includes lib_petal_march, which calls it.

#define PETAL_TAU 6.2831853
/** The golden angle, radians: each layer, row or frond turned by it from the last never lines up with another. */
#define PETAL_GOLD 2.3999632
#define PETAL_MAX_STEPS 96
/** uSteps (64..128 by Detail, 102 at its default, less on a hot phone) on this loop, so Detail and the hot-phone cut reach it. */
#define PETAL_STEPS (min(uSteps * 0.75, float(PETAL_MAX_STEPS)))
#define PETAL_HITS 4
#define PETAL_PERIOD 16.0
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define PETAL_RATE(k) (PETAL_TAU * float(k) / 7100.0)

float gPetalForm;
float gPetalFlow;
float gPetalChaos;
/** The low band's swell of the sway, 0..1.5 (see the header). */
float gPetalSwell;
/** How far the trace reaches, world units. */
float gPetalFar;
/** Half a sheet's thickness, world units. */
float gPetalThick;
/** How strongly the veins are drawn, 0 for none. */
float gPetalVeins;
float gPetalTone;
/** The share of the light behind a sheet seen square on that comes through it. */
float gPetalPass;
/** The share of a step taken: under 1 where a style's distance is only a bound. */
float gPetalStride;
/** The lamp, world space; with gPetalGap above 0, one every gPetalGap along z. */
vec3 gPetalLamp;
float gPetalGap;
/** How fast the lamp's glow falls off with distance (k in 1 / (1 + k r^2)), and how bright it is. */
float gPetalFall;
float gPetalShine;
vec3 gPetalPal0;
vec3 gPetalPal1;
vec3 gPetalPal2;
vec3 gPetalPal3;
vec3 gPetalPal4;

mat2 petalRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

float petalHash(vec2 c) {
    return fract(sin(dot(c, vec2(127.1, 311.7))) * 43758.5453);
}

/** The angle of v, radians; 0 where v is zero, where atan has none. */
float petalAtan(vec2 v) {
    return atan(v.y, abs(v.x) + abs(v.y) > 1.0e-6 ? v.x : 1.0);
}

/** A sheet cut to its outline: f the signed distance to the sheet's middle, o how far outside its outline (negative within). The sign keeps the side, so the normal is smooth across the sheet. */
float petalCut(float f, float o) {
    return (f < 0.0 ? -1.0 : 1.0) * max(abs(f), o);
}

/** The nearer of two sheets, each (distance, rim, vein). */
vec3 petalNear(vec3 a, vec3 b) {
    return abs(a.x) < abs(b.x) ? a : b;
}

/** A sheet's distance held under `bound` (the way out of a repeated cell, where a neighbour's sheet may be nearer), keeping its side. */
vec3 petalHold(vec3 m, float bound) {
    m.x = (m.x < 0.0 ? -1.0 : 1.0) * min(abs(m.x), max(bound, 0.05));
    return m;
}

/** The user's palette at t, from the five reads of petalSetup(): there and back, so no seam. */
vec3 petalCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gPetalPal0, gPetalPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gPetalPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gPetalPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gPetalPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

void petalSetup() {
    gPetalForm = neonForm();
    gPetalFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gPetalChaos = neonChaos();
    gPetalFar = 11.0 * (1.0 + 0.4 * neonKnob(5));
    gPetalThick = 0.01;
    gPetalVeins = 0.5 * (1.0 + neonKnob(1));
    gPetalTone = 0.6 * neonKnob(3);
    gPetalPass = 0.62 + 0.3 * neonKnob(4);
    gPetalStride = 0.75;
    gPetalLamp = vec3(0.0);
    gPetalGap = 0.0;
    gPetalFall = 2.5;
    gPetalShine = 0.4;
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gPetalSwell = sndShaped(SND_LOW) * drive;
    gPetalPal0 = pal(0.0);
    gPetalPal1 = pal(0.25);
    gPetalPal2 = pal(0.5);
    gPetalPal3 = pal(0.75);
    gPetalPal4 = pal(1.0);
}

/** Warp on space, a breeze: a curl about z that grows outward and comes and goes along the way, and a sway of the sheets (and the lows'). */
vec3 petalBend(vec3 p) {
    float zc = p.z * (PETAL_TAU / PETAL_PERIOD);
    float r = length(p.xy);
    p.xy = petalRot(gPetalFlow * 0.8 * sin(zc + 0.6 * r + uTime * PETAL_RATE(130))) * p.xy;
    float sway = 0.45 * min(gPetalFlow + 0.3 * gPetalSwell, 1.0);
    p.x += sway * sin(2.0 * zc + 1.2 * p.y + uTime * PETAL_RATE(200));
    p.y += 0.6 * sway * sin(zc + 1.4 * p.x + uTime * PETAL_RATE(170));
    return p;
}

/** The eye, held where the way is open: within `bore` of the axis, in the world's first period along z. */
vec3 petalEye(vec3 ro, float bore) {
    ro.z = mod(ro.z, PETAL_PERIOD);
    float r = length(ro.xy);
    if (r > bore) ro.xy *= bore / r;
    return ro;
}

/** The lamp that lights p: the one, or of a row along z the nearest. */
vec3 petalLampAt(vec3 p) {
    vec3 lamp = gPetalLamp;
    if (gPetalGap > 0.0) lamp.z += gPetalGap * round((p.z - lamp.z) / gPetalGap);
    return lamp;
}

/** One lamp's light along the ray from t = a to t = b: the exact integral of 1 / (1 + k r^2), r the distance to the lamp, in the depth haze. */
float petalGlowFrom(vec3 lamp, vec3 ro, vec3 rd, float a, float b) {
    vec3 l = lamp - ro;
    float tc = dot(l, rd);
    float s = sqrt(max(dot(l, l) - tc * tc, 0.0) + 1.0 / gPetalFall);
    float air = exp(-2.0 * clamp(tc, a, b) / gPetalFar);
    return air * (atan((b - tc) / s) - atan((a - tc) / s)) / (gPetalFall * s);
}

/** The lamps' light along the ray from a to b. A row of lamps gives the two ahead of the eye, each fading in from the far one and out near the eye, so none jumps and none fills the frame. */
float petalGlow(vec3 ro, vec3 rd, float a, float b) {
    if (gPetalGap <= 0.0) return petalGlowFrom(gPetalLamp, ro, rd, a, b);
    vec3 lamp = gPetalLamp;
    lamp.z += gPetalGap * (floor((ro.z - lamp.z) / gPetalGap) + 1.0);
    float dz = lamp.z - ro.z;
    float next = smoothstep(0.3, 2.0, dz + gPetalGap) * (1.0 - smoothstep(1.5 * gPetalGap, 2.0 * gPetalGap, dz + gPetalGap));
    return smoothstep(0.3, 2.0, dz) * petalGlowFrom(lamp, ro, rd, a, b) +
        next * petalGlowFrom(lamp + vec3(0.0, 0.0, gPetalGap), ro, rd, a, b);
}

float petalDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 petalFinish(vec3 col, float exposure) {
    col *= exposure * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    col = grade(neonAces(col));
    // No pixel leaves as a NaN or an infinity: the feedback pass would carry it for good.
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    return vec4(col, 1.0);
}
