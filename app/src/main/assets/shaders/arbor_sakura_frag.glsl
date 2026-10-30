#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_arbor

// Arbor Sakura: a cherry tree reaching across a pale spring sky in front of a
// hazy mountain, blossoms opening along its twigs and petals drifting down.
// Painted rather than drawn: soft pastel washes, a paper grain, edges that
// wobble a hair. Flat 2D: no march.
//
// The tree is arborTree2 with an open, lopsided fork and a strong lean, the
// cherry's sideways reach. Blossoms are discs at the outermost tips, and
// WHICH tips bloom is decided per branch from gArbPath, the fold's id for
// the twig a pixel belongs to, so every blossom is a particular twig's and
// holds still while the tree sways.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets OPEN BLOSSOMS. Each twig tip has a hashed place on a cycle and the
// onset count (uHitCount) slides a "flowering" window along it, so every
// onset opens a few new flowers and lets a few of the oldest fall: the tree
// is always about half in bloom, but WHERE it blooms keeps moving with the
// rhythm. The onset envelope (uHits.w) is the opening, new flowers growing
// from nothing while the departing ones shrink away, so nothing pops. Air
// (uBandsHi.z) thickens the drift of falling petals. Swell (uSwell) only
// warms the sky a little.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the petal fall and the branches' sway. Complexity:
// uMorph adds a fork level and so twice the tips (arbLevels2). Chaos:
// uTurbulence and uWarp add per-branch wander and a breeze flutter; uTwist
// curls the reach. Colour: sky, mountain, blossoms and petals from pal().
// Energy: uIntensity scales the air petals. Space: the view zoom and pan.

#define SK_LEVELS 8.0
#define SK_CYCLE 40.0
#define SK_WINDOW 0.55

void main() {
    vec2 uv = arbView();
    float px = max(length(fwidth(uv)), 1e-4);
    float t = uTime * uSpeed;
    float energy = uIntensity;
    float chaos = arbChaos();
    float swell = clamp(uSwell, 0.0, 1.5);
    float air = clamp(uBandsHi.z, 0.0, 1.0) * energy;
    float onset = clamp(uHits.w, 0.0, 1.0);
    float n = floor(uHitCount);

    float hue = 0.95 + 0.02 * sin(t * 0.03);
    float grain = vnoise3(vec3(uv * 60.0, 1.0)) * 0.5 + vnoise3(vec3(uv * 9.0, 2.0)) * 0.5;

    // ---- sky and mountain -----------------------------------------------------
    vec3 skyLo = mix(vec3(0.98, 0.90, 0.84), pal(hue) * 0.4 + 0.55, 0.4);
    vec3 skyHi = mix(vec3(0.78, 0.86, 0.95), pal(hue + 0.55) * 0.35 + 0.55, 0.4);
    vec3 col = mix(skyLo, skyHi, smoothstep(-0.6, 1.0, uv.y)) * (0.96 + 0.04 * swell);
    float mtn = -0.30 + 0.45 * exp(-abs(uv.x - 0.25) * 2.6) - 0.02 * sin(uv.x * 13.0);
    vec3 mtnCol = mix(col, mix(vec3(0.62, 0.66, 0.78), pal(hue + 0.6) * 0.6, 0.4), 0.55);
    col = mix(col, mtnCol, arbInk(uv.y - mtn, px * 3.0));
    float snow = arbInk(uv.y - mtn, px * 2.0) * smoothstep(0.02, 0.10, uv.y - 0.0) * step(mtn - 0.06, uv.y);
    col = mix(col, vec3(0.97), snow * 0.6);
    float hill = -0.86 + 0.06 * sin(uv.x * 2.0 + 1.0);
    col = mix(col, mix(vec3(0.55, 0.68, 0.45), pal(hue + 0.3) * 0.6, 0.3), arbInk(uv.y - hill, px * 2.0));

    // ---- the tree -------------------------------------------------------------
    float sway = 0.012 * sin(t * 0.6) + chaos * 0.02 * sin(t * 3.1) * sin(t * 1.7);
    vec2 wob = 0.0035 * vec2(vnoise3(vec3(uv * 30.0, 5.0)), vnoise3(vec3(uv * 30.0, 6.0))) - 0.00175;
    float levels = arbLevels2(SK_LEVELS, 1.0);
    vec2 base = vec2(-0.36, hill + 0.02);
    ArborForm2 f = ArborForm2(vec2(0.62, 0.34), vec2(0.74, 0.66), 0.07 + sway + 0.06 * uTwist,
                              0.40 + 0.5 * chaos, 0.34, 0.11, 0.68);
    float d = arborTree2(uv + wob - base, f, levels, 4.0);
    float tip = gArbTip;
    float path = gArbPath;

    vec3 bark = mix(vec3(0.24, 0.15, 0.16), pal(hue + 0.05) * 0.25, 0.3);
    col = mix(col, bark * (0.75 + 0.4 * grain), arbInk(d, px * 1.5));

    // ---- blossoms ------------------------------------------------------------------
    float hb = arbBranchHash(path, 0.0, 3.0);
    float phase = fract(hb - n / SK_CYCLE);
    float step1 = 1.0 / SK_CYCLE;
    float open = step(phase, SK_WINDOW);
    // Newest flowers (just inside the window) open as the onset envelope
    // falls; the ones that just left it shrink away on the same envelope.
    open *= mix(1.0, 1.0 - onset, step(SK_WINDOW - step1, phase));
    open += onset * step(1.0 - step1, phase);
    float bloomR = 0.024 * open * (0.8 + 0.4 * hash11(path * 0.31 + 2.0));
    vec3 petal = mix(vec3(1.0, 0.80, 0.86), pal(hue + 0.02) * 0.5 + 0.5, 0.45);
    float blossom = arbInk(tip - bloomR, px * 1.5) * step(0.001, bloomR);
    col = mix(col, petal * (0.9 + 0.12 * smoothstep(bloomR, 0.0, tip)), blossom);
    col = mix(col, pal(hue + 0.1) * 0.5 + vec3(0.4, 0.2, 0.2), arbInk(tip - bloomR * 0.25, px) * blossom);

    // ---- falling petals ------------------------------------------------------------
    for (int layer = 0; layer < 2; layer++) {
        float fl = float(layer);
        float cs = 0.16 + 0.08 * fl;
        vec2 fp = uv + vec2(0.12 * t * (0.6 + 0.3 * fl) + 0.05 * sin(t * 0.7 + uv.y * 3.0), t * (0.10 + 0.05 * fl));
        vec2 cell = floor(fp / cs);
        float h = hash13(vec3(cell, 13.0 + fl));
        vec2 lp = (fract(fp / cs) - 0.5) * cs;
        lp -= (vec2(hash13(vec3(cell, 14.0)), hash13(vec3(cell, 15.0))) - 0.5) * cs * 0.5;
        lp = rot2(t * (1.0 + h * 2.0) + h * 6.0) * lp;
        float e = length(lp * vec2(1.0, 2.2)) - 0.010 * (1.0 - 0.3 * fl);
        float show = step(1.0 - (0.20 + 0.45 * air), h);
        col = mix(col, petal, arbInk(e, px) * show * (0.9 - 0.3 * fl));
    }

    col *= 0.92 + 0.1 * grain;
    col *= 0.85 + 0.15 * smoothstep(2.0, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
