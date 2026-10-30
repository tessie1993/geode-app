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

// Arbor Bolt: a lightning strike forking down out of a churning storm sky
// over a dark ridge, rain slanting through. Flat 2D: no march, ignores uSteps.
//
// Lightning is a tree grown downward with a LOPSIDED fork: arborTree2's +x
// child is the main channel (long, barely turned) and the -x child the side
// spark (short, flung wide), and a large per-branch jitter hashed from the
// branch id puts every kink somewhere different. A new seed is a new bolt of
// the same character.
//
// Flash safety: the bolt's own brightness never moves with a hit. A new bolt
// GROWS in down the sky while the old one fades out on the same envelope, so
// the light in the frame is handed over, not added.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets STRIKE. uHitCount is the bolt's seed: every onset is a new strike,
// with its own channel, kinks, side sparks and landing point. The onset
// envelope (uHits.w, 0.22 s) is the strike's progress, the new bolt racing
// down from the cloud while the last one fades. Bass (uBassSmooth) churns the
// clouds, air (uBandsHi.z) thickens the rain. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clouds, rain and the bolt's crawl. Complexity:
// uMorph adds a fork level to every strike (arbLevels2). Chaos: uTurbulence
// and uWarp widen the per-branch kink jitter into a wilder, more ragged bolt.
// Colour: the halo, the cloud light and the sky from pal(); the core stays
// near white. Energy: uIntensity scales the cloud churn and rain. Space:
// the view zoom and pan.

#define BO_LEVELS 9.0
#define BO_TOP 1.15

float bolt(vec2 uv, float n, float grow, float chaos, float t) {
    float x0 = (hash11(n * 1.7 + 0.3) - 0.5) * 0.5;
    vec2 p = -(uv - vec2(x0, BO_TOP));
    float lean = 0.03 * sin(t * 1.7 + n) + (hash11(n * 2.9) - 0.5) * 0.12;
    ArborForm2 f = ArborForm2(vec2(0.14, 0.80), vec2(0.86, 0.46), lean, 0.95 + 0.6 * chaos, 0.30, 0.022, 0.85);
    return arborTree2(p, f, grow, n * 13.37);
}

void main() {
    vec2 uv = arbView();
    float px = max(length(fwidth(uv)), 1e-4);
    float t = uTime * uSpeed;
    float energy = uIntensity;
    float chaos = arbChaos();
    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(uBassSmooth, 0.0, 1.5) * energy;
    float air = clamp(uBandsHi.z, 0.0, 1.0) * energy;
    float onset = clamp(uHits.w, 0.0, 1.0);
    float n = floor(uHitCount);

    float hue = 0.74 + 0.03 * sin(t * 0.04);

    // ---- storm sky ----------------------------------------------------------
    vec3 cp = vec3(uv * vec2(1.4, 2.2), t * (0.04 + 0.05 * bass));
    cp.x += t * 0.03;
    float cloud = fbm3(cp + vec3(0.0, 0.0, fbm3(cp * 1.7, 2) * (0.6 + 0.8 * bass)), 4);
    vec3 col = mix(pal(hue) * 0.03 + vec3(0.01, 0.008, 0.02), pal(hue + 0.05) * 0.22,
                   smoothstep(0.35, 0.8, cloud) * smoothstep(-0.6, 0.9, uv.y));

    // ---- the strike -----------------------------------------------------------
    float levels = arbLevels2(BO_LEVELS, 1.0);
    float dNew = bolt(uv, n, 0.5 + levels * (1.0 - onset), chaos, t);
    float dOld = bolt(uv, n - 1.0, levels, chaos, t);
    float aNew = 1.0 - 0.6 * onset;
    float aOld = onset;

    vec3 core = mix(vec3(1.0), pal(hue + 0.1), 0.15);
    vec3 halo = pal(hue + 0.12);
    float nearNew = max(dNew, 0.0);
    float nearOld = max(dOld, 0.0);
    // The bolt lights the cloud base around it.
    col += halo * cloud * (exp(-nearNew * 3.0) * aNew + exp(-nearOld * 3.0) * aOld) * 0.25;
    col += halo * (exp(-nearNew * 28.0) * aNew + exp(-nearOld * 28.0) * aOld) * 0.35;
    col += halo * (exp(-nearNew * 7.0) * aNew + exp(-nearOld * 7.0) * aOld) * 0.06;
    col = mix(col, core, arbInk(dNew, px * 0.8) * aNew);
    col = mix(col, core, arbInk(dOld, px * 0.8) * aOld);

    // ---- rain and ridge -----------------------------------------------------
    vec2 rg = vec2(uv.x * 70.0 + uv.y * 12.0, uv.y * 3.0 + t * 9.0);
    float drop = hash13(vec3(floor(rg), 2.0));
    float streak = step(1.0 - 0.06 * (0.3 + air), drop) * smoothstep(0.5, 0.0, abs(fract(rg.x) - 0.5));
    col += pal(hue + 0.05) * streak * 0.08;

    float ridge = -0.88 + 0.05 * sin(uv.x * 3.1 + 0.7) + 0.02 * sin(uv.x * 11.0);
    col = mix(col, vec3(0.006, 0.005, 0.012), arbInk(uv.y - ridge, px));

    col *= 0.88 + 0.15 * swell;
    col *= 0.8 + 0.2 * smoothstep(2.0, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
