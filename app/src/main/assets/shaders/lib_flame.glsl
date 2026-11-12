// The Fractal Flame family: thin luminous veils and threads on black, the look
// of a flame fractal, drawn as real 3D structures the camera rig flies through
// or turns about.
//
// WHAT IS DRAWN. Each style gives one closed-form field, flameField(p): two
// scalar functions of space. A veil is a level surface of one of them (every
// whole number and a half), so a function that winds, folds or nests gives
// veils that do. Nothing is solid: the march samples the ray and, between two
// samples, adds the light of every veil the ray crossed on the way, from the
// line profile's exact integral. So veils behind show through the ones in
// front, a veil seen along its surface is brighter than one seen square on, a
// step may be coarse without losing or aliasing a veil, and far veils too fine
// to tell apart gather into an even glow instead of noise. A veil's width is a
// share of the gap to its neighbour, so the web is as sharp at every scale. A
// style may ask for threads instead (gFlameThread): one set of veils drawn
// only where it meets the other.
//
// COST. One loop of min(uSteps, 72) samples (the Detail budget), each one
// flameField: folds, logs and sines, at most three fold iterations, no noise
// octaves and no texture. The palette is read five times per pixel before the
// loop (flameSetup) and mixed inside it. No distance estimate, so no step is
// spent creeping up on a surface; no normals; no second march.
//
// THE CONTROLS, read normalized through lib_neon so Customize, the macros, the
// director and the DNA reach every style the same way:
//
//   Morph       gFlameForm   the structure's own metamorphosis, named by each style
//   Warp        gFlameFlow   flameBend(): the whole structure twists about the
//                            way ahead and sways sideways
//   Turbulence  gFlameChaos  a restless ripple through the veils
//   Intensity   neonGlow()   the emission
//   k0 PULSE the emission, k1 TEXTURE the veils' sharpness, k2 FLOW the bend,
//   k3 TONE the palette position, k4 BUILD how many veils show, k5 SPACE the reach.
//
// THE SOUND, heard the way the older families hear it (lib_dive, lib_neon):
// lib_sound's shaped reads, in flameSetup(), on the motion only, never on a
// colour, a brightness or a beat.
//
//   gFlameTurn   the mid band, sndShaped(SND_MIDDLE), pushes the whole structure
//   round about the way ahead while the mids climb and eases back as they
//   fall, as lib_dive's gDiveSpin does. Mid gain scales it.
//   gFlameSwell  the low band, sndShaped(SND_LOW), widens the Warp's sideways sway,
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
// The world repeats along z every FLAME_PERIOD (16, dividing the rig's 8192),
// and every clock phase is whole cycles per uTime wrap (FLAME_RATE).
//
// INCLUDE ORDER: last, after lib_scene_uniforms (with VIEW_OWNS_SHAPE),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon.
// The style defines flameField after it.

#define FLAME_TAU 6.2831853
#define FLAME_MAX_STEPS 72
#define FLAME_PERIOD 16.0
/** k whole cycles per uTime wrap (7100 s, ShaderScene), so sin(uTime * FLAME_RATE(k)) never jumps. */
#define FLAME_RATE(k) (FLAME_TAU * float(k) / 7100.0)

float gFlameForm;
float gFlameFlow;
float gFlameChaos;
/** The mid band's push on the twist about z, radians, and the low band's swell of the bend, 0..1.5 (see the header). */
float gFlameTurn;
float gFlameSwell;
/** A veil's half-width as a share of the gap to the next one. */
float gFlameWidth;
/** How far the march reaches, world units. */
float gFlameFar;
/** The even mist between the veils, 0..1. */
float gFlameHaze;
/** 0 draws whole veils; 1 only the threads where the two sets cross. */
float gFlameThread;
/** How much a veil seen square on is dimmed, 0..1, and the field's change per unit of ray that counts as square on. */
float gFlameEdge;
float gFlameGraze;
/** How many veils of each set go once round, for a set that winds about an axis: its veils' looks repeat with it. */
vec2 gFlamePeriod;
float gFlameTone;
/** Veils whose weight is under this are not drawn (BUILD lowers it). */
float gFlameGate;
vec3 gFlamePal[5];

/**
 * The style's field at p, two sets of veils. A veil of set i lies wherever f[i]
 * passes a whole number and a half; hue[i] is its palette position and wt[i]
 * its weight, 0..1 (0 for a set the style does not use).
 */
void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt);

mat2 flameRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/** The user's palette at t, from the five reads of flameSetup(): there and back, so no seam. */
vec3 flameCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    int i = int(min(floor(x), 3.0));
    return mix(gFlamePal[i], gFlamePal[i + 1], smoothstep(0.0, 1.0, x - float(i)));
}

void flameSetup() {
    gFlameForm = neonForm();
    gFlameFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gFlameChaos = neonChaos();
    gFlameWidth = 0.035 * (1.0 - neonKnob(1));
    gFlameFar = 9.0 * (1.0 + 0.5 * neonKnob(5));
    gFlameHaze = 0.015;
    gFlameEdge = 1.0;
    gFlamePeriod = vec2(4096.0);
    gFlameGraze = 0.4;
    gFlameThread = 0.0;
    gFlameTone = 0.6 * neonKnob(3);
    gFlameGate = 0.12 - 0.24 * neonKnob(4);
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gFlameTurn = 0.6 * sndShaped(SND_MIDDLE) * drive;
    gFlameSwell = sndShaped(SND_LOW) * drive;
    for (int i = 0; i < 5; i++) gFlamePal[i] = pal(0.25 * float(i));
}

/** Warp and Turbulence on the structure: a twist about z that comes and goes along the way (and the mids' push), a sideways sway (and the lows' swell), a ripple. */
vec3 flameBend(vec3 p) {
    float zc = p.z * (FLAME_TAU / FLAME_PERIOD);
    p.xy = flameRot(gFlameFlow * 1.1 * sin(zc + uTime * FLAME_RATE(170)) + gFlameTurn) * p.xy;
    p.x += 0.55 * min(gFlameFlow + 0.3 * gFlameSwell, 1.0) * sin(2.0 * zc + 0.9 * p.y + uTime * FLAME_RATE(240));
    p.y += gFlameFlow * 0.4 * cos(zc - 0.7 * p.x);
    p += gFlameChaos * 0.09 * sin(p.yzx * 3.3 + uTime * FLAME_RATE(1900) + vec3(0.0, 2.1, 4.2));
    return p;
}

float flameDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** The line profile's running integral: it rises by exactly 1 across each veil, almost all of it within w of the veil. */
vec2 flameAnti(vec2 x, float w) {
    vec2 c = fract(x) - 0.5;
    return floor(x) + 0.5 + c * inversesqrt(c * c + w * w) * sqrt(0.25 + w * w);
}

/** The line profile itself (flameAnti's slope), for a ray that runs along a veil. */
vec2 flameLine(vec2 x, float w) {
    vec2 c = fract(x) - 0.5;
    vec2 q = c * c + w * w;
    return w * w * sqrt(0.25 + w * w) * inversesqrt(q) / q;
}

/** How near x is to a veil, 1 on it, on a wider profile: where the other set's veils show as threads. */
vec2 flameNear(vec2 x) {
    vec2 c = fract(x) - 0.5;
    return 0.004 / (c * c + 0.004);
}

/** The gathered light along one ray. */
vec3 flameMarch(vec3 ro, vec3 rd) {
    float n = min(uSteps, float(FLAME_MAX_STEPS));
    float w = gFlameWidth;
    vec3 acc = vec3(0.0);
    float tPrev = 0.0;
    vec2 fPrev = vec2(0.0);
    vec2 aPrev = vec2(0.0);
    float jitter = flameDither();
    for (int i = 0; i < FLAME_MAX_STEPS; i++) {
        if (float(i) >= n) break;
        float x = (float(i) + jitter) / n;
        float t = gFlameFar * x * sqrt(x);
        vec2 f;
        vec2 hue;
        vec2 wt;
        flameField(flameBend(ro + rd * t), f, hue, wt);
        vec2 a = flameAnti(f, w);
        if (i > 0) {
            // Between two samples the field is taken as a straight line, so the light of every veil
            // crossed on the way is exact: a coarse step loses no veil and none aliases.
            // An angle's wrap is a jump by whole veils, not veils crossed.
            vec2 jump = round(f - fPrev) * step(1.5, abs(f - fPrev));
            fPrev += jump;
            aPrev += jump;
            vec2 df = abs(f - fPrev);
            vec2 e = mix(abs(a - aPrev) / max(df, 1.0e-4), flameLine(f, w), step(df, vec2(2.0e-3)));
            // A veil seen along its surface is the bright one: the silhouettes are the filaments.
            vec2 rate = df / ((t - tPrev) * gFlameGraze);
            e *= mix(vec2(1.0), 2.5 / (1.0 + rate * rate), gFlameEdge);
            e = mix(e, e * flameNear(f.yx) * 4.0, gFlameThread) + gFlameHaze;
            // Nothing lights right at the eye: flying through a veil must not fill the frame.
            float fade = smoothstep(0.05, 1.0, t) * exp(-2.6 * t / gFlameFar) * (t - tPrev);
            vec2 id = fract((mod(floor(0.5 * (f + fPrev)), gFlamePeriod) + 1.0) * 0.381966);
            // Every veil has its own brightness, so the layers read apart.
            vec2 amp = smoothstep(gFlameGate, gFlameGate + 0.3, wt) * (0.12 + 0.88 * id * id) * e * fade;
            acc += flameCol(hue.x + id.x + gFlameTone) * amp.x + flameCol(hue.y + id.y + gFlameTone) * amp.y;
        }
        tPrev = t;
        fPrev = f;
        aPrev = a;
    }
    return acc;
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 flameFinish(vec3 col, float exposure) {
    col *= 0.3 * exposure * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.25), 0.0);
    return vec4(grade(neonAces(col)), 1.0);
}
