// Depth layers and formulas for the marched families that include it (Loom,
// Petal, Fold): what lies in front of and behind a style's own raymarched
// form, and which of its formulas builds that form.
//
// THREE LAYERS. The style's map is the foreground, sphere traced by its
// family's march. This file adds the two behind and around it:
//
//   midground   layerMid(): a domain-warped fractal haze sampled at
//               LAYER_MID_SAMPLES points between the eye and the first thing
//               the ray meets; it glows and dims what lies behind it.
//   background  layerFar(): a fractal far off, seen by direction alone (no
//               parallax, so it reads as distant), wherever the foreground
//               and the haze let light through.
//
// Each is a different fractal under each formula (below), so the three never
// read as one look:
//
//   formula      midground                 background
//   0            warped gyroid sheets      a Menger-like lattice
//   1            a kaleidoscopic IFS cloud a Mandelbox-like slice
//   2            an apollonian foam        an apollonian (sphere-inversion) web
//
// Each moves on its own: the haze drifts sideways, breathes and, being in the
// world, is flown through along the camera's path; the far layer drifts and
// breathes on slower clocks. Nothing here turns with the sound (owner
// 2026-11-12).
//
// FORMULA. uFormula (0..1, lib_scene_uniforms) picks one of three formulas:
// gLayerPick holds their weights, 0 or 1 except in a narrow crossfade band
// about 0.25 and 0.75, where two neighbours are mixed (their distances, never
// a hard switch). A style's map evaluates only the formulas whose weight is
// above 0, so outside the bands it costs one formula, inside them two. The
// reaction matrix moves Formula with the music (song section, chord, phrase);
// nothing here reads a sound band for it.
//
// ITERATION. gLayerIter (0..1) is how far a fractal's iteration has grown: up
// with Detail (uSteps, so the hot-phone cut lowers it too), k1 TEXTURE and k4
// BUILD, and Fractal (uFractal) on top, toward the full count. layerIters(lo, hi) turns it into a fractional count; iteration i is
// applied by the share layerShare(count, i), mixed with what came before, so
// a growing count never pops. The far layers take fewer iterations than the
// foreground.
//
// THE MUSIC, through the knobs and the params, each layer its own:
//   foreground  Morph, k1 TEXTURE, Formula (the style's map)
//   midground   k2 FLOW and Warp the warp, k4 BUILD the density, the low
//               band's shaped swell (gLayerSwell, motion only) the warp's reach
//   background  k5 SPACE its scale, k3 TONE its hue
//
// ESCAPE COLOUR (lib_shade, which the family library includes first). Wherever a layer's fractal folds, its
// orbit trap (how near the fold's orbit came to its centre or its plane) moves that layer's hue along the
// palette by Escape colour (uEscapeColor): the midground's IFS cloud and apollonian foam, and all three far
// fractals. The gyroid sheets fold nothing and keep their hue.
//
// COST. LAYER_MID_SAMPLES (6) haze samples of at most 4 fold iterations and
// one far evaluation of at most 4, twice each inside a crossfade band; the far
// layer is skipped where nothing of it shows.
//
// INCLUDE ORDER: after lib_scene_uniforms, lib_palette, lib_neon and lib_shade; the
// family library includes it at its end, and its march calls layerSetup()
// before the first map and layerCompose() on the way out.

#define LAYER_TAU 6.2831853
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define LAYER_RATE(k) (LAYER_TAU * float(k) / 7100.0)
#define LAYER_MID_SAMPLES 6
/** The world's period along z (the families' 16): every field here repeats with it. */
#define LAYER_PERIOD 16.0

/** The three formulas' weights (see the header). */
vec3 gLayerPick = vec3(1.0, 0.0, 0.0);
/** How far the fractals' iteration has grown, 0..1. */
float gLayerIter = 0.5;
float gLayerMid;
float gLayerWarp;
float gLayerFar;
float gLayerTone;
vec3 gLayerPal0;
vec3 gLayerPal1;
vec3 gLayerPal2;
vec3 gLayerPal3;
vec3 gLayerPal4;

mat2 layerRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/** The weights of the three formulas at f: a plateau on each, a smooth crossfade 0.12 wide about 0.25 and 0.75. */
vec3 layerPickOf(float f) {
    float x = clamp(f, 0.0, 1.0);
    float a = smoothstep(0.19, 0.31, x);
    float b = smoothstep(0.69, 0.81, x);
    return vec3(1.0 - a, a - b, b);
}

/** A fractional iteration count from lo to hi as the iteration grows. */
float layerIters(float lo, float hi) {
    return mix(lo, hi, gLayerIter);
}

/** How much of iteration i a fractional count applies, 0..1. */
float layerShare(float count, int i) {
    return clamp(count - float(i), 0.0, 1.0);
}

/** Mirror repetition along z with the world's period, so a field never seams where the eye wraps. */
vec3 layerWrapZ(vec3 p) {
    p.z = 0.5 * LAYER_PERIOD - abs(mod(p.z, LAYER_PERIOD) - 0.5 * LAYER_PERIOD);
    return p;
}

vec3 layerCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gLayerPal0, gLayerPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gLayerPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gLayerPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gLayerPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

/**
 * The family's palette reads and controls, once per pixel before its march. `flow` is the family's
 * Warp (with k2 FLOW), `swell` its low band's shaped push (motion only).
 */
void layerSetup(vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec3 p4, float tone, float flow, float swell) {
    gLayerPick = layerPickOf(uFormula);
    float detail = clamp((uSteps - 64.0) / 64.0, 0.0, 1.0);
    gLayerIter = clamp(0.2 + 0.5 * detail + 0.6 * neonKnob(1) + 0.4 * neonKnob(4), 0.0, 1.0);
    // Fractal (uFractal, lib_grow's param) grows it on top, into the headroom left: styles read gLayerIter
    // as a 0..1 mix too, so it never passes 1, and at Fractal 0 it is exactly the count above.
    gLayerIter += (1.0 - gLayerIter) * clamp(uFractal, 0.0, 1.0);
    gLayerMid = clamp(1.0 + 1.4 * neonKnob(4), 0.2, 2.0);
    gLayerWarp = 0.35 + 0.55 * flow + 0.3 * min(swell, 1.0);
    gLayerFar = 1.0 + 0.6 * neonKnob(5);
    gLayerTone = tone + 0.5 * neonKnob(3);
    gLayerPal0 = p0;
    gLayerPal1 = p1;
    gLayerPal2 = p2;
    gLayerPal3 = p3;
    gLayerPal4 = p4;
}

/** Smooth union, rounded k wide where the two meet. */
float layerSmin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

/** Smooth intersection. */
float layerSmax(float a, float b, float k) {
    return -layerSmin(-a, -b, k);
}

/**
 * A two-pass domain warp of the foreground, `amt` world units at most: z enters only at whole cycles of
 * the world's period, so a warped world still repeats with it. It stretches space by at most
 * 1 + 2.5 amt; layerWarpSlack(amt) is the share of a distance that stays a safe step.
 */
vec3 layerWarp(vec3 p, float amt) {
    float zc = p.z * (LAYER_TAU / LAYER_PERIOD);
    vec3 q = p + amt * sin(vec3(1.3 * p.y + 3.0 * zc, 1.1 * p.x + 2.0 * zc + 1.7, 0.9 * p.x + 1.2 * p.y + 2.9) +
        uTime * vec3(LAYER_RATE(110), LAYER_RATE(150), LAYER_RATE(90)));
    zc = q.z * (LAYER_TAU / LAYER_PERIOD);
    return q + 0.45 * amt * sin(vec3(2.3 * q.y - 5.0 * zc + 0.6, 2.1 * q.z + 1.9 * q.x, 2.5 * q.x - 3.0 * zc + 4.1) -
        uTime * vec3(LAYER_RATE(230), LAYER_RATE(170), LAYER_RATE(260)));
}

float layerWarpSlack(float amt) {
    return 1.0 / (1.0 + 2.5 * amt);
}

/** A fractal's light domain warp: `flow` (Warp and k2) and `chaos` (Turbulence) set its reach; returns the step's slack. */
float layerFlowWarp(inout vec3 p, float flow, float chaos) {
    float amt = 0.03 + 0.08 * flow + 0.04 * chaos;
    p = layerWarp(p, amt);
    return layerWarpSlack(amt);
}

/** p.xy folded into one of n mirrored sectors about the axis (n >= 1): the sector's middle on +x. */
vec2 layerSector(vec2 q, float n) {
    float a = abs(q.x) + abs(q.y) > 1.0e-6 ? atan(q.y, q.x) : 0.0;
    float w = LAYER_TAU / n;
    a = abs(a - w * floor(a / w + 0.5));
    return length(q) * vec2(cos(a), sin(a));
}

/**
 * A kaleidoscopic fold of p, `count` iterations (fractional: the last one mixed in by its share): each
 * mirrors p into the positive octant, shifts it by -shift, turns it by `ang` about z and then x and grows
 * it by `grow`. `scale` is the total growth, so a distance found in the folded space divided by it is a
 * distance here.
 */
vec3 layerKifs(vec3 p, float count, vec3 shift, float ang, float grow, out float scale) {
    scale = 1.0;
    mat2 r1 = layerRot(ang);
    mat2 r2 = layerRot(0.7 * ang + 0.3);
    for (int i = 0; i < 5; i++) {
        float a = layerShare(count, i);
        if (a <= 0.0) break;
        vec3 f = abs(p) - shift;
        f.xy = r1 * f.xy;
        f.yz = r2 * f.yz;
        p = mix(p, f * grow, a);
        scale *= mix(1.0, grow, a);
    }
    return p;
}

/** The midground's density at p, 0..1, and in hue its palette offset: one fractal per formula, in a warped domain. */
float layerMidField(vec3 p, out float hue) {
    p = layerWrapZ(p);
    // Breathing and a slow sideways drift.
    p *= 1.0 + 0.1 * sin(uTime * LAYER_RATE(97));
    p.xy += 0.6 * vec2(sin(uTime * LAYER_RATE(53)), cos(uTime * LAYER_RATE(41)));
    // Two passes of domain warp; z only at whole cycles of the period, so the field repeats with the world.
    float w = gLayerWarp;
    vec3 q = p + w * 0.55 * sin(p.yzx * vec3(0.9, 0.7, 1.1) + vec3(0.0, 1.7, 3.1) + uTime * LAYER_RATE(130));
    q += w * 0.3 * sin(q.zxy * vec3(1.9, 2.3, 1.7) + vec3(2.3, 0.4, 5.1) - uTime * LAYER_RATE(170));
    float dens = 0.0;
    hue = 0.0;
    if (gLayerPick.x > 0.0) {
        // Sheets along a gyroid's zero set, broken into drifts.
        vec3 g3 = q * 0.95;
        float g = dot(sin(g3), cos(g3.yzx));
        dens += gLayerPick.x * exp(-5.0 * g * g) * (0.45 + 0.55 * sin(0.5 * q.x + 0.4 * q.y + 1.3));
        hue += gLayerPick.x * 0.12 * q.y;
    }
    if (gLayerPick.y > 0.0) {
        // A kaleidoscopic IFS: fold, shift, turn and grow; its orbit's nearness to a sphere is the cloud.
        vec3 k = q * 0.55;
        float s = 1.0;
        float trap = 1.0e3;
        float n = layerIters(2.0, 4.0);
        for (int i = 0; i < 4; i++) {
            float a = layerShare(n, i);
            if (a <= 0.0) break;
            vec3 f = abs(k) - vec3(0.8, 0.55, 0.7);
            f.xy = layerRot(0.62) * f.xy;
            f.yz = layerRot(0.41) * f.yz;
            k = mix(k, 1.55 * f, a);
            s *= mix(1.0, 1.55, a);
            trap = min(trap, abs(length(k) - 0.9) / s);
        }
        dens += gLayerPick.y * exp(-8.0 * trap);
        hue += gLayerPick.y * (0.3 * clamp(trap, 0.0, 1.0) + shadeEscape(shadeTrapPoint(64.0 * trap * trap)));
    }
    if (gLayerPick.z > 0.0) {
        // An apollonian foam: fold into a cell, invert in its sphere, again; the shells' walls are the cloud.
        vec3 k = q * 0.4;
        float s = 1.0;
        float n = layerIters(2.0, 4.0);
        // The orbit trap: the least |f|^2 the cells' inversions saw, each iteration's share by its fraction.
        float orbit = 3.0;
        for (int i = 0; i < 4; i++) {
            float a = layerShare(n, i);
            if (a <= 0.0) break;
            vec3 f = -1.0 + 2.0 * fract(0.5 * k + 0.5);
            float inv = 1.15 / max(dot(f, f), 0.08);
            k = mix(k, f * inv, a);
            s *= mix(1.0, inv, a);
            orbit = mix(orbit, min(orbit, dot(f, f)), a);
        }
        float wall = 0.3 * abs(k.y) / s;
        dens += gLayerPick.z * exp(-30.0 * wall);
        hue += gLayerPick.z * (0.25 + 0.15 * k.x / s + shadeEscape(shadeTrapPoint(orbit)));
    }
    return dens;
}

/** The midground along the ray from the eye to tEnd: its light, and in a the share of what lies behind that it lets through. */
vec4 layerMid(vec3 ro, vec3 rd, float tEnd, float far) {
    // The midground is the middle distance: from just past the eye to about half the reach.
    float a = 0.4;
    float b = min(tEnd, 0.55 * far);
    if (b <= a) return vec4(0.0, 0.0, 0.0, 1.0);
    float dt = (b - a) / float(LAYER_MID_SAMPLES);
    // White noise, not a pattern: a patterned offset shows as hatching across the haze.
    float jit = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    vec3 light = vec3(0.0);
    float through = 1.0;
    for (int i = 0; i < LAYER_MID_SAMPLES; i++) {
        float t = a + (float(i) + jit) * dt;
        float hue;
        float d = layerMidField(ro + rd * t, hue) * gLayerMid;
        // Nothing thickens right at the eye: flying through the haze must not fill the frame.
        float fade = smoothstep(0.3, 1.6, t) * exp(-2.2 * t / far);
        light += through * d * fade * dt * 0.09 * layerCol(gLayerTone + 0.2 + 0.3 * t / far + hue);
        through *= exp(-0.05 * d * fade * dt);
    }
    return vec4(light, through);
}

/** The far layer's light seen along rd: a fractal on a far shell, its fine edges lit. */
vec3 layerFar(vec3 rd) {
    vec3 p = rd * (2.4 * gLayerFar);
    p *= 1.0 + 0.08 * sin(uTime * LAYER_RATE(61));
    p += 0.35 * vec3(sin(uTime * LAYER_RATE(23)), sin(uTime * LAYER_RATE(31) + 1.0), cos(uTime * LAYER_RATE(19)));
    float glow = 0.0;
    float hue = 0.0;
    // Fewer iterations than the foreground: 2 to 3.
    float n = layerIters(2.0, 3.0);
    if (gLayerPick.x > 0.0) {
        // A Menger-like lattice: a cube's crosses cut out at three scales.
        vec3 q = p * 0.7;
        float d = -1.0e3;
        float s = 1.0;
        // The orbit trap: the least |m|^2 the folded cells came to, each scale's share by its fraction.
        float orbit = 3.0;
        for (int i = 0; i < 3; i++) {
            float a = layerShare(n, i);
            if (a <= 0.0) break;
            vec3 m = mod(q * s, 2.0) - 1.0;
            s *= 3.0;
            vec3 r = abs(1.0 - 3.0 * abs(m));
            float c = (min(max(r.x, r.y), min(max(r.y, r.z), max(r.z, r.x))) - 1.0) / s;
            d = i == 0 ? c : mix(d, max(d, c), a);
            orbit = mix(orbit, min(orbit, dot(m, m)), a);
        }
        glow += gLayerPick.x * exp(-60.0 * abs(d));
        hue += gLayerPick.x * (0.1 * q.z + shadeEscape(shadeTrapPoint(orbit)));
    }
    if (gLayerPick.y > 0.0) {
        // A Mandelbox-like slice: box fold, ball fold, scale and add; the orbit's nearness to a plane is lit.
        vec3 q = p * 0.6;
        vec3 c = q;
        float trap = 1.0e3;
        for (int i = 0; i < 4; i++) {
            float a = layerShare(n + 1.0, i);
            if (a <= 0.0) break;
            vec3 f = clamp(q, -1.0, 1.0) * 2.0 - q;
            float r2 = dot(f, f);
            f *= clamp(1.0 / max(r2, 1.0e-4), 1.0, 4.0);
            f = -1.6 * f + c;
            q = mix(q, f, a);
            trap = min(trap, abs(q.y) / (1.0 + 0.4 * float(i)));
        }
        glow += gLayerPick.y * exp(-14.0 * trap);
        hue += gLayerPick.y * (0.25 * clamp(length(q) * 0.1, 0.0, 1.0) + shadeEscape(shadeTrapPoint(16.0 * trap * trap)));
    }
    if (gLayerPick.z > 0.0) {
        // An apollonian web: the walls between inverted spheres.
        vec3 q = p * 1.35 + vec3(0.3, 0.7, 0.1);
        float s = 1.0;
        // The orbit trap: the least |f|^2 the cells' inversions saw, each iteration's share by its fraction.
        float orbit = 3.0;
        for (int i = 0; i < 4; i++) {
            float a = layerShare(n + 1.0, i);
            if (a <= 0.0) break;
            vec3 f = -1.0 + 2.0 * fract(0.5 * q + 0.5);
            float inv = 1.2 / max(dot(f, f), 0.08);
            q = mix(q, f * inv, a);
            s *= mix(1.0, inv, a);
            orbit = mix(orbit, min(orbit, dot(f, f)), a);
        }
        glow += gLayerPick.z * exp(-90.0 * abs(0.25 * q.y / s));
        hue += gLayerPick.z * (0.4 + shadeEscape(shadeTrapPoint(orbit)));
    }
    return layerCol(gLayerTone + 0.6 + hue) * (0.012 + 0.11 * glow);
}

/**
 * A march's light with the layers laid round it: the haze in front of tEnd (where the ray stopped, or
 * the reach), and the far layer through `open`, the share of the ray that met nothing.
 */
vec3 layerCompose(vec3 col, vec3 ro, vec3 rd, float tEnd, float far, float open) {
    vec4 mid = layerMid(ro, rd, tEnd, far);
    col = col * mid.a + mid.rgb;
    if (open > 0.01) col += layerFar(rd) * open * mid.a;
    return col;
}
