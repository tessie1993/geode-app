#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the plasma field itself (below), not
// on the screen, so view() must not bend the rays a second time.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Plasma: the classic demo-scene plasma, a sum of interfering sine waves read
// through the palette, lifted into three dimensions and flown through. The 2D
// style summed four waves over the plane (two axis waves, a diagonal one and a
// ring wave about the centre); here the same four, plus a fifth running down
// the travel axis, are summed over space, and the region where the sum rises
// above a level is solid. That level set is a lava-lamp labyrinth of smooth
// blobs and arches that breathe as the waves slide. The surface glows with a
// second plasma (the 2D formula itself, now evaluated at the hit point), so
// the colour bands of the old style crawl over the blobs, and the air between
// them carries the plasma's glow, gathered along the march.
//
// The camera is the shared rig (camFly). A tube around the z axis is smoothly
// carved out of the field as the flight channel, and a sphere around the eye
// out of everything, so the eye is never inside a blob whatever path the rig
// takes. The ring wave of the 2D style is now concentric tubes around that
// channel.
//
// ---- the wrap ---------------------------------------------------------------
//
// Every z frequency is an integer multiple of PL_U = 2 pi / 32, so every term
// repeats every 32 world units along z, which divides the rig's 8192 travel
// wrap. The eye is rebased by mod(z, 32) before the march, so z stays precise
// and neither the wrap nor the rebase can show. x and y are not wrapped.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (SND_BASS)            the ring wave's frequency, as in the 2D style: the
//                            tubes around the channel tighten on the low end.
// Kick (SND_KICK_ENV)        the blobs swell: the solid level drops (PULSE scales it).
// Snare (SND_SNARE_ENV)      the glow in the air flares.
// Hat (SND_HAT_ENV)          the plasma's colour contours light up as bright lines.
// Spectrum                   the colour offset per band of the plasma, as in the
//                            2D style (sndSpectrum(fract(v * 0.25 + 0.5))).
// Volume (SND_VOL_ATT)       the overall glow, as in the 2D style.
// Pitch (SND_HELD_NOTE_CHANGE) slides the hue, as a hump.
// Drop (SND_HELD_SURGE)      the channel opens and the palette swings to its far
//                            side, on a hump, so it glides in and out.
// Mids (SND_MIDDLE)          turn the eye's look aside while the mids climb and
//                            ease it back as they fall, as lib_terra's
//                            terraTurn and lib_truchet's truchetTurn do: 0.1 x
//                            sndShaped x SND_DRIVE screen units (0.15 at most,
//                            0 in silence), the gFlameTurn band on a structure
//                            that holds still. Mid gain scales it.
//
// ---- controls ---------------------------------------------------------------
//
// Morph       blends the plane waves toward a gyroid, so the blobs knit into a lattice.
// Warp        a sine domain warp of space, the 2D style's swirl in 3D.
// Turbulence  a fine sixth wave that ripples the blob skins.
// Knobs: k0 PULSE kick swell, k1 TEXTURE colour band density, k2 FLOW warp depth,
// k3 TONE hue, k4 BUILD air glow, k5 SPACE channel width. All neutral at 0.5.
//
// ---- why the distance estimate is safe --------------------------------------
//
// The field f is a weighted sum of sines sin(k . p + phase); each has gradient
// magnitude |k|, the gyroid term at most sqrt(6) times its frequency, so |grad f|
// is bounded by the weighted sum of those magnitudes (gLip, computed per frame
// from the live bass, morph and turbulence). (level - f) / gLip therefore
// never overestimates the distance to the level set. The warp's offset has one
// sine per axis, each of slope at most PL_WK * w, so its Jacobian has norm at
// most 1 + PL_WK * w, and the estimate is divided by that as well. The channel
// and the eye bubble join through smax and max, which keep the bound.

#define PL_MAX_STEPS 110
#define PL_FAR 30.0
/** 2 pi / 32: the z frequency unit. Every z term is an integer multiple. */
#define PL_U 0.19634954
#define PL_ZWRAP 32.0
#define PL_BUBBLE 0.85
/** The warp's largest frequency (its x, y and z rates are 0.9, 4 PL_U, 1.1). */
#define PL_WK 1.1

float gRingK;
float gMorph;
float gTurb;
float gWarp;
float gLevel;
float gTube;
float gLip;
float gT;
vec3 gEye;
/** Set by map(): the field's value at the last point, for the glow. */
float gF;

vec3 warpP(vec3 p) {
    if (gWarp < 0.001) return p;
    return p + gWarp * sin(vec3(p.y * 0.9 + gT * 1.1, p.z * (4.0 * PL_U) - gT * 0.8, p.x * 1.1 + gT * 0.9));
}

/** The 3D plasma: five sine waves, a gyroid by Morph and a fine ripple by Turbulence. */
float field(vec3 p) {
    float t = gT;
    float r = length(p.xy);
    float waves = sin(p.x + t)
                + sin(p.y * 1.15 - t * 1.2)
                + sin((p.x + p.y) * 0.85 + p.z * (4.0 * PL_U) + t * 0.7)
                + sin(r * gRingK - t * 2.0)
                + sin(p.x * 0.6 + p.z * (6.0 * PL_U) - t * 0.9);
    float f = waves * 0.2 * (1.0 - gMorph);
    if (gMorph > 0.001) {
        vec3 q = p * (4.0 * PL_U) + vec3(0.0, 0.0, t * 0.3);
        f += gMorph * dot(sin(q), cos(q.yzx)) / 1.5;
    }
    if (gTurb > 0.001) {
        f += gTurb * 0.15 * sin(dot(p, vec3(2.3, 2.9, 12.0 * PL_U)) + t * 1.3);
    }
    return f;
}

float map(vec3 w) {
    vec3 p = warpP(w);
    float f = field(p);
    gF = f;
    float d = (gLevel - f) / gLip;
    // The flight channel, blended in so it reads as a cave, not a cut.
    d = smax(d, gTube - length(w.xy), 0.8);
    d = max(d, PL_BUBBLE - length(w - gEye));
    return d;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** The 2D style's plasma, evaluated in space: the colour that crawls over the blobs. */
float plasmaColour(vec3 p, float tex) {
    float t = gT;
    vec2 uv = p.xy * tex;
    float v = sin(uv.x * 1.7 + t) + sin(uv.y * 1.9 - t * 1.2)
            + sin((uv.x + uv.y) * 1.3 + p.z * (8.0 * PL_U) + t * 0.7)
            + sin(length(p.xy) * gRingK * 1.4 - t * 2.0);
    return v * 0.25;
}

void main() {
    vec2 uv = view();

    float bass = sndLevel(SND_BASS);
    float vol = sndLevel(SND_VOL_ATT);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);

    float kPulse = uKnob[0] - 0.5;
    float kTex = uKnob[1] - 0.5;
    float kFlow = uKnob[2] - 0.5;
    float kTone = uKnob[3] - 0.5;
    float kBuild = uKnob[4] - 0.5;
    float kSpace = uKnob[5] - 0.5;

    gT = uTime * 0.6;
    gRingK = 1.4 + 1.4 * clamp(bass, 0.0, 1.5);
    gMorph = clamp(uMorph, 0.0, 1.0) * 0.6;
    gTurb = clamp(uTurbulence, 0.0, 2.0);
    gWarp = clamp(uWarp * 0.6 + kFlow * 0.4, 0.0, 1.2);
    gLevel = (0.34 - 0.24 * kick * (1.0 + kPulse)) * (1.0 + 0.4 * dropBump);
    gTube = (1.7 + 1.2 * dropBump) * (1.0 + 0.5 * kSpace);

    // The bound on |grad f| (see the header): the five wave magnitudes are
    // 1, 1.15, |(0.85, 0.85, 4U)| = 1.437, the ring frequency, and
    // |(0.6, 0, 6U)| = 1.322; the gyroid's sqrt(6) * 4U / 1.5 = 1.283; the
    // ripple's |(2.3, 2.9, 12U)| * 0.15 = 0.66.
    float waveLip = (1.0 + 1.15 + 1.437 + gRingK + 1.322) * 0.2;
    gLip = ((1.0 - gMorph) * waveLip + gMorph * 1.283 + gTurb * 0.66) * (1.0 + PL_WK * gWarp);
    gLip = max(gLip, 0.05);

    vec3 ro;
    vec3 rd;
    // The mids turn the look aside (see the header).
    camFly(uv - vec2(0.1 * sndShaped(SND_MIDDLE) * clamp(SND_DRIVE, 0.0, 1.0), 0.0), ro, rd);
    // Every z term repeats every 32 units (see the header): exact and invisible.
    ro.z = mod(ro.z, PL_ZWRAP);
    gEye = ro;

    float hue = 0.03 * sin(uTime * 0.021) + 0.22 * pitchBump + 0.5 * dropBump + 0.4 * kTone;
    float glowGain = (0.010 + 0.022 * snare) * (1.0 + 1.2 * kBuild) * (0.6 + 0.8 * vol);

    float t = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    vec3 glow = vec3(0.0);
    for (int i = 0; i < PL_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > PL_FAR) break;
        used = float(i);
        vec3 p = ro + rd * t;
        float d = map(p);
        // The air glows where the field nears the level: the plasma's halo.
        float near = exp(-max(d, 0.0) * 4.0);
        glow += pal(hue + gF * 1.2 + 0.25) * near * glowGain * exp(-t * 0.06);
        float eps = 0.0008 * t + 0.0006;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * 0.85, eps);
    }

    float fogK = 0.06;
    float ahead = pow(max(rd.z, 0.0), 6.0);
    vec3 fogCol = pal(hue + 0.55) * (0.03 + 0.10 * ahead) + vec3(0.004, 0.002, 0.006);
    vec3 col = fogCol;

    // The 2D style's colour rule, kept: the plasma value plus the spectrum at
    // that value's place, through the palette. Read outside the hit branch so
    // the texture fetch sits in uniform control flow.
    vec3 p = ro + rd * (hitT > 0.0 ? hitT : PL_FAR);
    float v = plasmaColour(warpP(p), 1.0 + 0.8 * kTex);
    v += sndSpectrum(fract(v * 0.25 + 0.5)) * 0.4;

    if (hitT > 0.0) {
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
        vec3 L = normalize(vec3(0.3, 0.6, 0.75));
        float dif = max(dot(n, L), 0.0);
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 36.0);

        vec3 base = pal(hue + v);
        vec3 emit = base * (0.35 + 0.75 * clamp(vol, 0.0, 1.2));

        // Hats light the plasma's contours: thin lines where the value crosses
        // a band edge, widened by the pixel's footprint so they do not alias.
        float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
        float band = abs(fract(v * 3.0) - 0.5) * 2.0;
        float line = 1.0 - smoothstep(0.0, 0.10 + px * 6.0, band);
        emit += mix(base, vec3(1.0), 0.5) * line * (0.05 + 0.9 * hat);

        vec3 surf = emit * (0.55 + 0.45 * ao)
                  + base * dif * 0.25 * ao
                  + pal(hue + v + 0.5) * fres * 0.45
                  + vec3(spec) * 0.25 * ao;
        col = mix(surf, fogCol, 1.0 - exp(-hitT * fogK));
    }

    col += glow;
    col *= 0.75 + 0.3 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
