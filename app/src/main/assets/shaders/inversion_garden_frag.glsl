#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the map below, not on the plane.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade

// Inversion Garden: a 2D escape-free fractal grown by iterating one small map
// of the plane - mirror, grow, turn inside out through a circle - and colouring
// each pixel by how close its orbit came to a ring or a line. The result is
// lace: nested lenses and petals that breathe as the map's offset wanders.
//
// ---- the map ---------------------------------------------------------------
//
// Per pixel, p starts at view() (zoom, rotation, drift and the endless-zoom
// layer all apply) and is iterated up to IG_MAX_ITERS times:
//
//   1. fold:   p.x = |p.x|, a mirror; a second, partial fold on p.y gives the
//              four-way (kaleidoscopic) variant. The partial form,
//              mix(p.y, |p.y|, w), keeps the picture continuous while w moves.
//   2. grow:   p = p * s + o, s a little above 1, o on a closed curve (a
//              figure-of-eight) so the garden drifts and comes back.
//   3. invert: p -> c + r^2 (p - c) / |p - c|^2, the textbook inversion in the
//              circle of centre c and radius r. Inversion swaps inside and
//              outside, which is what keeps the orbit bounded with no escape
//              test: whatever grows too far is sent back in.
//
// Two traps are kept along the orbit: the nearest it came to a ring of radius
// IG_RING about c, and the nearest it came to the line y = 0. Turbulence mixes
// them. The orbit's final length sets where on the palette a pixel sits.
//
// ---- controls --------------------------------------------------------------
//
// - Morph: the growth factor s, and past ~0.85 the second fold creeps in.
// - Warp: the radius of the offset's figure-of-eight.
// - Turbulence: ring trap -> line trap.
// - Detail (uSteps, the Detail budget): the iteration depth, 7..14. A hot
//   phone's Settings › Performance lowers uSteps and so the depth.
// - Zoom, Rotation, Drift, Kaleidoscope, Endless zoom: view().
//
// The six knobs (neutral at 0.5, read as offsets from it):
// k0 PULSE   how far the bass swells the growth factor
// k1 TEXTURE the contour bands: how many, and how fine
// k2 FLOW    how fast the offset travels its curve
// k3 TONE    where on the palette the garden sits
// k4 BUILD   how wide the offset's curve opens
// k5 SPACE   the inversion circle's radius: tighter lace or looser petals
//
// ---- sound -----------------------------------------------------------------
//
// - The bass (SND_BASS_ATT, already attenuated) and a share of SND_KICK_ENV
//   swell s by a few percent: the lace breathes on the beat without jumping.
// - The key (uKeyHue, eased over 3 s, gated by uKeyStrength) turns the palette.
// - SND_HAT_ENV and SND_ONSET_ENV light the contour lines, thin and local,
//   never the whole frame.
// - SND_BUILD_UP widens the offset curve, so a build makes the garden roam.
// - In a drop section (SND_SECTION_STATE) the second fold eases in over the
//   first bar and out again by the eighth, so a drop blooms into four-way
//   symmetry and settles back.
// - The offset travels on uFlowPhase, which loudness speeds up and slows down
//   but never reverses.
// Brightness never follows a trigger directly; uFlash belongs to the composite.
//
// Provenance: written from scratch for MuzViz from textbook maths only - the
// reflection fold, a similarity map, inversion in a circle and orbit traps.
// No published shader was consulted.

#define IG_TAU 6.2831853
/** Compile-time ceiling on the iteration count; uSteps picks the depth under it. */
#define IG_MAX_ITERS 14
/** The inversion circle's centre (c). Off the fold axis' origin so the lace is not radially dull. */
#define IG_CENTER vec2(0.0, 0.18)
/** The radius of the ring trap about IG_CENTER. */
#define IG_RING 0.62
/** The section state the sound engine calls a drop (GEODE_SECTION_DROP). */
#define IG_SECTION_DROP 3.0

float igKnob(int i) { return clamp(uKnob[i], 0.0, 1.0) - 0.5; }

// How much of the second (y) fold to apply: Morph's top end, or a drop's bloom.
float igSecondFold() {
    float byMorph = smoothstep(0.85, 1.0, clamp(uMorph, 0.0, 1.0));
    // Bars into the section, smooth inside a bar thanks to the bar phase.
    float bars = SND_SECTION_BARS + SND_BAR_PHASE;
    float inDrop = 1.0 - step(0.5, abs(SND_SECTION_STATE - IG_SECTION_DROP));
    float byDrop = inDrop * smoothstep(0.0, 1.0, bars) * (1.0 - smoothstep(6.0, 8.0, bars));
    return max(byMorph, byDrop);
}

void main() {
    vec2 uv = view();

    float morph = clamp(uMorph, 0.0, 1.0);
    float warp = clamp(uWarp, 0.0, 1.0);
    float chaos = clamp(uTurbulence / 1.5, 0.0, 1.0);

    // ---- the map's parameters ---------------------------------------------
    float swell = sndLevel(SND_BASS_ATT) * 0.6 + 0.4 * sndShaped(clamp(SND_KICK_ENV, 0.0, 1.0));
    float s = mix(1.02, 1.22, morph) + swell * (0.035 + 0.05 * igKnob(0));

    float build = clamp(SND_BUILD_UP, 0.0, 1.0);
    float roam = 0.06 + 0.16 * warp + 0.10 * build + 0.12 * igKnob(4);
    float t = uFlowPhase * (0.045 + 0.05 * igKnob(2)) + uTime * 0.01;
    // A figure-of-eight about a resting offset: x turns once while y turns twice.
    vec2 o = vec2(-0.82, -0.36) + roam * vec2(cos(t), 0.6 * sin(2.0 * t));

    float r = 0.95 + 0.3 * igKnob(5);
    float r2 = r * r;
    float fold2 = igSecondFold();

    // ---- iterate ----------------------------------------------------------
    // uSteps runs 24..128 (64..128 with Detail at its default range); 96 -> 12.
    float iters = clamp(floor(4.0 + uSteps / 12.0), 7.0, float(IG_MAX_ITERS));
    vec2 p = uv * 1.35;
    float ringTrap = 1e3;
    float lineTrap = 1e3;
    float glow = 0.0;
    for (int i = 0; i < IG_MAX_ITERS; i++) {
        if (float(i) >= iters) break;
        p.x = abs(p.x);
        p.y = mix(p.y, abs(p.y), fold2);
        p = p * s + o;
        vec2 d = p - IG_CENTER;
        // Clamped so a point that lands on the centre is sent far, not to infinity.
        p = IG_CENTER + d * (r2 / max(dot(d, d), 1e-3));

        float ring = abs(length(p - IG_CENTER) - IG_RING);
        float line = abs(p.y);
        ringTrap = min(ringTrap, ring);
        lineTrap = min(lineTrap, line);
        // A soft sum of near passes: the bodies between the contour lines.
        glow += exp(-12.0 * mix(ring, line, chaos));
    }
    float trap = mix(ringTrap, lineTrap, chaos);
    float len = length(p);
    glow /= iters;

    // ---- colour -----------------------------------------------------------
    float key = 0.35 * uKeyHue * clamp(uKeyStrength, 0.0, 1.0);
    float hue = 0.32 * log(1.0 + len) + 2.4 * trap + key + 0.5 * igKnob(3);
    vec3 body = pal(fract(hue));
    vec3 edge = pal(fract(hue + 0.33));

    // The lace: brighter the closer the orbit came to the trap.
    float lace = exp(-14.0 * trap);
    vec3 col = body * (0.03 + 1.4 * lace) + edge * glow * 0.8;

    // Contour bands on the trap value: thin bright lines, their count and
    // sharpness from TEXTURE, a pixel-aware width so they never alias to noise.
    float bands = 18.0 + 22.0 * igKnob(1);
    float phase = trap * bands - uFlowPhase * 0.25;
    float wave = abs(fract(phase) - 0.5);
    float w = max(fwidth(phase), 1e-4) * (1.2 - 0.6 * igKnob(1));
    float contour = 1.0 - smoothstep(0.0, w * 1.5, wave - 0.02);
    // Fade the bands where they crowd under a pixel apart, so they average out instead of shimmering.
    contour *= 1.0 - smoothstep(0.25, 0.5, fwidth(phase));
    float sparkle = 0.35 + 0.65 * sndShaped(max(clamp(SND_HAT_ENV, 0.0, 1.0), 0.6 * clamp(SND_ONSET_ENV, 0.0, 1.0)));
    col += mix(edge, vec3(1.0), 0.35) * contour * lace * sparkle * 0.55;

    // A soft vignette around the garden and a soft shoulder, so the
    // brightest folds roll off rather than clip before grade() sees them.
    col *= 1.0 - 0.35 * smoothstep(0.6, 1.8, length(uv));
    col = 1.0 - exp(-col * 1.15);

    fragColor = vec4(grade(col), 1.0);
}
