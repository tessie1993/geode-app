#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_shell

// Spectrum Tunnel: a real tube, flown through. The old style drew glowing
// rings on a closed-form cylinder; now the rings are ribs, a solid ribbed
// shell around the world's z axis, lit by a headlight on the eye, with fog
// down the bore and the far end glowing where the rays never land.
//
// The tube is a shell (|wall - r| < thickness), not a solid outside a hole,
// so from the rig's orbit and wide glide paths, where the eye leaves the bore,
// it still reads as a lit pipe from the outside rather than a wall of black.
//
// ---- camera ------------------------------------------------------------------
//
// camFly: travel down the bore is the Camera tab's fly speed, and the rig's
// dolly (kick punches, drop pushes) is already in uCamPos, so the lurch down
// the tube comes with it. The eye's z is rebased by TN_ZWRAP; every z term in
// the field and the shading has a period that divides it (ribs 2, spectrum
// spiral 8, wobble 16, bend and hue 32, bend 64), and 64 divides the rig's
// 8192 travel wrap, so neither the rebase nor the wrap ever shows.
//
// ---- audio -------------------------------------------------------------------
//
// Spectrum (sndEq)      each rib's brightness rides the band under it, read
//                       on a spiral around and down the tube, as before; the
//                       loud bands also swell their ribs a little inward.
// Kick (SND_KICK_ENV)   launches a ripple down the tube: a front that leaves
//                       the eye on the hit and runs away as the envelope
//                       falls, shoving the ribs along z and squeezing the
//                       bore as it passes. It moves the ribs, never their light.
// High (SND_HIGH)       the waveform glows in the grooves between the ribs,
//                       the old wave overlay laid on the wall.
// Drop (SND_HELD_SURGE) swaps the palette to its far side, on a hump so it glides.
// SND_ENERGY_TREND      the far end's glow and the overall level.
//
// Params: Morph rounds the ribs from sharp crests to broad swells, Warp twists
// the spectrum spiral and bends the bore, Turbulence wobbles the cross-section,
// Intensity and the palette reach through lib_scene_grade and pal().
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the ripple's
// strength, k1 TEXTURE the groove wave, k2 FLOW the rib light, k3 TONE the
// ribs' depth (a high melody carves them into deep fins, a low one smooths the
// bore toward a plain pipe; and a small hue offset), k4 BUILD the far glow,
// k5 SPACE the bore radius.
//
// ---- shape inputs (lib_shell) -------------------------------------------------
//
// Every tunable of the tube is a normalized input, mapped to its range in one
// place (the top of main) and read from globals. 0.5 is the look as tuned: the
// knobs rest there exactly while Note & key is off, and a param at its default
// 0 maps to its rest through shellMapS. Music reaches these only through the
// knobs and params (the reaction matrix); none reads the sound.
//
//   input       driver           lo .. rest .. hi
//   sReach      k0 PULSE         how far down the tube the ripple runs 9 .. 14 .. 20
//   sShell      k1 TEXTURE       the shell's half-thickness 0.18 .. 0.3 .. 0.5
//   sLobes      k1 TEXTURE       the wobble's lobes round the bore 2 .. 3 .. 5 (whole)
//   gRibAmp     k3 TONE          rib depth 0.036 .. 0.18 .. 0.324
//   gRadius     k5 SPACE         bore radius 3.8 .. 4.4 .. 5.0
//   sFog        k5 SPACE         fog density 0.07 .. 0.045 .. 0.025
//   gMorph      Morph            sharp crests to broad swells 0 .. 1
//   gTwist      Warp             the spectrum spiral's twist 0 .. 0.25
//   gBend       Warp             the bore's bend 0 .. 0.7
//   gWobble     Turbulence       the cross-section's wobble 0 .. 0.4
//
// The rib spacing (2) and every other z period stay constants: each divides
// the 64 the eye is rebased by. The lobes are a whole number, so the wobble
// closes round the bore without a seam. The shell goes through SHELL_FIELD3,
// so the shell's spawn, fractal, front and back hooks reach it; the empty-
// middle skip holds only while those hooks are at rest.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// The field is the shell's radial gap divided by TN_LIP, a bound on its
// gradient. The rib profile c^3 of a cosine has slope at most 0.97 per radian
// of rib phase; the phase turns TAU / 2 per world unit, plus at most 0.38 from
// the kick ripple at full PULSE, so the 0.3-high ribs slope at most ~1.6 along
// z. The kick squeeze (0.36), the wobble (0.08) and the bend (0.14) add theirs,
// ~2.2 along z in all, and the band swell and the wobble ~0.6 around the wall:
// sqrt(1 + 2.2^2 + 0.6^2) < 2.5; when TONE deepens the ribs past 0.3 the
// bound grows in proportion (gLip). The spectrum lookup is
// mirrored, so the band height has no seam to cliff on. A sphere around the
// eye is carved out of the shell, so the eye is never inside it. The march
// starts where the ray leaves the inner cylinder no part of the shell can
// reach (its radius is the bore minus every inward term at its current size),
// so the steps go to the wall, not to the empty middle.

#define TN_MAX_STEPS 96
#define TN_TAU 6.2831853
/** Rebase period of the eye's z; divides 8192. */
#define TN_ZWRAP 64.0
/** Bore radius at rest, world units: wider than the glide path's usual wander. */
#define TN_RADIUS 4.4
/** Half-thickness of the shell. */
#define TN_SHELL 0.3
/** World units per rib. */
#define TN_RIB 2.0
/** How far down the tube (world units) the kick ripple has travelled once its envelope is spent. */
#define TN_REACH 14.0
/** Gradient bound of the radial field (see the header). */
#define TN_LIP 2.5
#define TN_FAR 48.0
#define TN_BUBBLE 0.8

vec3 gEye;
float gKick;
float gKickAmp;
float gRibAmp;
float gMorph;
float gTwist;
float gBend;
float gWobble;
float gRadius;
/** Shape inputs (see the header): the ripple's reach, the shell's half-thickness, the wobble's lobes, fog. */
float sReach;
float sShell;
float sLobes;
float sFog;
/** The radial field's gradient bound: TN_LIP, scaled up when TONE deepens the ribs. */
float gLip;
/** Set by map(): the rib cosine (1 on a crest), the band under the point, its angle 0..1. */
float gC;
float gBand;
float gAng;

/** The bore's centre at depth z: the Warp bend, periods 32 and 64. */
vec2 boreCentre(float z) {
    return gBend * vec2(sin(z * (TN_TAU / 32.0)), cos(z * (TN_TAU / 64.0)));
}

/** The analyser band on the spiral at angle `ang` (0..1) and depth z, mirrored so it has no seam. */
float bandAt(float ang, float z) {
    float x = fract(ang + z * 0.125 + gTwist * sin(z * (TN_TAU / 32.0)));
    return clamp(0.5 * sndEq(abs(2.0 * x - 1.0)), 0.0, 1.0);
}

float mapField(vec3 p) {
    vec2 q = p.xy - boreCentre(p.z);
    float r = length(q);
    float ang = atan(q.y, q.x) / TN_TAU + 0.5;
    gAng = ang;

    // The kick ripple: 1 at the hit, so the front starts at the eye and runs
    // away down the tube as the envelope decays. Measured from the eye, so the
    // rebase shifts both sides alike.
    float dF = (p.z - gEye.z) - (1.0 - gKick) * sReach;
    float env = exp(-abs(dF) * 0.6);
    float ripple = gKickAmp * 0.12 * sin(dF * 1.5) * env;

    float c = 0.5 + 0.5 * cos(TN_TAU * (p.z / TN_RIB + ripple));
    gC = c;
    float band = bandAt(ang, p.z);
    gBand = band;
    float prof = mix(c * c * c, c, gMorph);
    float inward = prof * (gRibAmp + 0.12 * band)
                 + gKickAmp * 0.4 * env
                 + gWobble * (0.5 + 0.5 * sin(ang * TN_TAU * sLobes + p.z * (TN_TAU / 16.0) + uTime * 0.3));
    float wall = gRadius - inward;
    float d = (abs(wall - r) - sShell) / gLip;
    return max(d, TN_BUBBLE - length(p - gEye));
}

/** The tube as the shell sees it: spawn, fractal, front and back route through here (identity at rest). */
float map(vec3 p) {
    return SHELL_FIELD3(p, mapField);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    // ---- shape inputs: each normalized input mapped to its range, here only (see the header).
    sReach = shellMap(shellKnob(0), 9.0, TN_REACH, 20.0);
    sShell = shellMap(shellKnob(1), 0.18, TN_SHELL, 0.5);
    sLobes = floor(shellMap(shellKnob(1), 2.0, 3.0, 5.0) + 0.5);
    gRibAmp = shellMap(shellKnob(3), 0.036, 0.18, 0.324);
    gRadius = shellMap(shellKnob(5), TN_RADIUS - 0.6, TN_RADIUS, TN_RADIUS + 0.6);
    sFog = shellMap(shellKnob(5), 0.07, 0.045, 0.025);
    gMorph = shellMapS(clamp(uMorph, 0.0, 1.0), -1.0, 0.0, 1.0);
    gTwist = shellMapS(clamp(uWarp, 0.0, 1.0), -0.25, 0.0, 0.25);
    gBend = shellMapS(clamp(uWarp, 0.0, 1.0), -0.7, 0.0, 0.7);
    gWobble = shellMapS(clamp(uTurbulence, 0.0, 1.0), -0.4, 0.0, 0.4);

    vec2 uv = view();

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float high = sndShaped(SND_HIGH);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);

    gKick = clamp(SND_KICK_ENV, 0.0, 1.0);
    gKickAmp = gKick * clamp(1.0 + kPulse, 0.5, 1.5);
    gLip = TN_LIP * max(1.0, (gRibAmp + 0.12) / 0.3);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64, so this shift is exact
    // and invisible, and z keeps its precision far down the travel.
    ro.z = mod(ro.z, TN_ZWRAP);
    gEye = ro;

    // Skip the empty middle: no part of the shell comes nearer the axis than
    // the bore minus every inward term at its current size. Only while the
    // shell's hooks rest: a spawned or folded copy can stand in the middle.
    float rin = gRadius - (gRibAmp + 0.12 + gKickAmp * 0.4 + gWobble + gBend + sShell + 0.05);
    float t = 0.03;
    float a = dot(rd.xy, rd.xy);
    float cc = dot(ro.xy, ro.xy) - rin * rin;
    bool shellAtRest = abs(SHELL_SPAWN) + abs(SHELL_FRACTAL) + abs(SHELL_FRONT) + abs(SHELL_BACK) == 0.0;
    if (rin > 0.5 && cc < 0.0 && shellAtRest) {
        if (a < 1e-6) {
            t = TN_FAR;
        } else {
            float b = dot(ro.xy, rd.xy);
            t = max(t, (-b + sqrt(max(b * b - a * cc, 0.0))) / a);
        }
    }

    float hitT = -1.0;
    float used = 0.0;
    float haze = 0.0;
    for (int i = 0; i < TN_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > TN_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        // The lit crests haze the air they pass close to.
        haze += smoothstep(0.6, 1.0, gC) * gBand * exp(-max(d, 0.0) * 6.0);
        float eps = 0.0008 * t + 0.0008;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * 0.9, eps);
    }

    float hue0 = 0.12 * kTone + 0.5 * dropBump;
    float ahead = pow(max(dot(rd, normalize(uCamBasis[2])), 0.0), 10.0);
    vec3 fogCol = pal(hue0 + 0.3) * (0.025 + (0.22 + 0.25 * swell) * (1.0 + kBuild) * ahead) + vec3(0.003, 0.002, 0.006);
    vec3 col = fogCol;
    float lightK = 1.0 + 1.2 * kFlow;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        // Restore the hit point's globals after the normal taps.
        map(p);

        float hue = hue0 + 0.5 + 0.5 * sin(p.z * (TN_TAU / 32.0));
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 0.8, 0.35, 1.0) * (0.55 + 0.45 * gC);
        // Headlight on the eye: falls off down the bore.
        float fall = 1.0 / (1.0 + 0.03 * hitT * hitT);
        float spec = pow(ndv, 24.0);

        vec3 wall = pal(hue + 0.55) * (0.05 + 0.22 * ndv * fall) * ao
                  + pal(hue + 0.2) * fres * 0.12
                  + vec3(spec) * 0.25 * fall;

        // The rings: each crest glows with the band under it.
        float crest = smoothstep(0.55, 0.95, gC);
        wall += pal(hue) * (0.15 + gBand * 1.6) * crest * lightK;

        // The waveform in the grooves, the old treble overlay.
        float groove = 1.0 - smoothstep(0.0, 0.15, gC);
        float wave = sndWave01(fract(gAng * 2.0));
        wall += pal(0.6 + hue0) * wave * high * 0.6 * groove * clamp(1.0 + 1.2 * kTexture, 0.2, 2.0);

        col = mix(wall, fogCol, 1.0 - exp(-hitT * sFog));
    }

    col += pal(hue0 + 0.1) * haze * 0.03 * lightK;
    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv) * uZoom));
    fragColor = vec4(grade(col), 1.0);
}
