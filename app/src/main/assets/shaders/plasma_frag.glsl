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
//#include lib_shell
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
//
// ---- controls ---------------------------------------------------------------
//
// Morph       blends the plane waves toward a gyroid, so the blobs knit into a lattice.
// Warp        a sine domain warp of space, the 2D style's swirl in 3D.
// Turbulence  a fine sixth wave that ripples the blob skins.
// Knobs: k0 PULSE kick swell, k1 TEXTURE colour band density, k2 FLOW warp depth,
// k3 TONE the grain of the plasma (a high melody draws the blobs up into tall
// columns, a low one lays them out in long horizontal strata; a small hue
// offset too), k4 BUILD air glow, k5 SPACE channel width. All neutral at 0.5.
// Every one of these, and the solid level (BUILD: the blobs fatten), the ring
// wave's rest frequency (TEXTURE) and the fog (SPACE), is one normalized input
// (lib_shell) set in one block at the top of main and mapped there to its
// range; at rest each is the constant the plasma was tuned with. The labyrinth
// is the field the grow hooks act on (its shell wrapper): Spawn slides a second
// labyrinth through the first, Fractal grows blobs a third the size on its
// skins, the layers open the near and far labyrinth.
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

/** TONE: the per-axis scale of the field (1 at rest; never above 1). */
vec3 gGrain;

/** The labyrinth and its channel: the field the grow hooks act on (map() hands it to its shell wrapper). */
float plasmaField(vec3 w) {
    // TONE's grain: each factor is at most 1, so gLip still bounds the gradient.
    vec3 p = warpP(w) * gGrain;
    float f = field(p);
    gF = f;
    float d = (gLevel - f) / gLip;
    // The flight channel, blended in so it reads as a cave, not a cut.
    return smax(d, gTube - length(w.xy), 0.8);
}

/** plasmaField through lib_shell's hooks: exactly plasmaField(p) at rest, and plasmaField read once, in a loop the compiler cannot unroll. */
float plasmaFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 7; i++) {
        if (shellPass3(i)) r = shellKeep3(i, plasmaField(shellAt3(i)));
    }
    return r;
}

float map(vec3 w) {
    return max(plasmaFieldGrown(w), PL_BUBBLE - length(w - gEye));
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

    // ---- the shape inputs (lib_shell): each 0..1, 0.5 the tuned look --------------------
    float nPulse = shellKnob(0);                                   // PULSE    the kick's swell
    float nTexture = shellKnob(1);                                 // TEXTURE  colour band density, the ring wave
    float nFlow = shellKnob(2);                                    // FLOW     the warp's depth
    float nTone = shellKnob(3);                                    // TONE     the plasma's grain, the hue
    float nBuild = shellKnob(4);                                   // BUILD    the air glow, the blobs' level
    float nSpace = shellKnob(5);                                   // SPACE    the channel's width, the fog
    float nMorph = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);            // Morph    plane waves to a gyroid
    float nWarp = 0.5 + 0.5 * clamp(uWarp, 0.0, 1.0);              // Warp     the domain warp
    float nTurb = 0.5 + 0.5 * clamp(uTurbulence / 1.5, 0.0, 1.0);  // Turbulence the fine ripple
    float swellK = shellMap(nPulse, 0.5, 1.0, 1.5);
    float tex = shellMap(nTexture, 0.6, 1.0, 1.4);
    float ringBase = shellMap(nTexture, 1.1, 1.4, 1.8);
    float warpFlow = shellMap(nFlow, -0.2, 0.0, 0.2);
    // TONE's grain: each factor is at most 1.
    float grainX = shellMap(nTone, 0.35, 1.0, 1.0);
    float grainY = shellMap(nTone, 1.0, 1.0, 0.35);
    float hueTone = shellMap(nTone, -0.2, 0.0, 0.2);
    float glowK = shellMap(nBuild, 0.4, 1.0, 1.6);
    float level = shellMap(nBuild, 0.42, 0.34, 0.26);
    float tubeK = shellMap(nSpace, 0.75, 1.0, 1.25);
    float fogK = shellMap(nSpace, 0.075, 0.06, 0.045);
    gMorph = shellMap(nMorph, 0.0, 0.0, 0.6);
    float warpWarp = shellMap(nWarp, 0.0, 0.0, 0.6);
    gTurb = shellMap(nTurb, 0.0, 0.0, 1.5);

    gT = uTime * 0.6;
    gGrain = vec3(grainX, grainY, 1.0);
    gRingK = ringBase + 1.4 * clamp(bass, 0.0, 1.5);
    gWarp = clamp(warpWarp + warpFlow, 0.0, 1.2);
    gLevel = (level - 0.24 * kick * swellK) * (1.0 + 0.4 * dropBump);
    gTube = (1.7 + 1.2 * dropBump) * tubeK;

    // The bound on |grad f| (see the header): the five wave magnitudes are
    // 1, 1.15, |(0.85, 0.85, 4U)| = 1.437, the ring frequency, and
    // |(0.6, 0, 6U)| = 1.322; the gyroid's sqrt(6) * 4U / 1.5 = 1.283; the
    // ripple's |(2.3, 2.9, 12U)| * 0.15 = 0.66.
    float waveLip = (1.0 + 1.15 + 1.437 + gRingK + 1.322) * 0.2;
    // (gRingK carries TEXTURE's rest frequency, so the bound follows it.)
    gLip = ((1.0 - gMorph) * waveLip + gMorph * 1.283 + gTurb * 0.66) * (1.0 + PL_WK * gWarp);
    gLip = max(gLip, 0.05);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term repeats every 32 units (see the header): exact and invisible.
    ro.z = mod(ro.z, PL_ZWRAP);
    gEye = ro;
    // The layers are measured from the eye; a spawned labyrinth slides 2 down the flight.
    shellSetup3(ro, 3.0, 2.0, false);

    float hue = 0.03 * sin(uTime * 0.021) + 0.22 * pitchBump + 0.5 * dropBump + hueTone;
    float glowGain = (0.010 + 0.022 * snare) * glowK * (0.6 + 0.8 * vol);

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

    float ahead = pow(max(rd.z, 0.0), 6.0);
    vec3 fogCol = pal(hue + 0.55) * (0.03 + 0.10 * ahead) + vec3(0.004, 0.002, 0.006);
    vec3 col = fogCol;

    // The 2D style's colour rule, kept: the plasma value plus the spectrum at
    // that value's place, through the palette. Read outside the hit branch so
    // the texture fetch sits in uniform control flow.
    vec3 p = ro + rd * (hitT > 0.0 ? hitT : PL_FAR);
    float v = plasmaColour(warpP(p), tex);
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
