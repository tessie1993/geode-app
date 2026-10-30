#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_lucid

// Lucid Lattice: a quasicrystal folded through itself. Seven plane waves at
// equal angles sum to a pattern with seven-fold symmetry that never repeats -
// a Penrose-like order with no tile. Here that sum is the nest's field: each
// of three levels reads the crystal, and its analytic gradient (the waves'
// derivative, so no extra taps) displaces where the next level reads. The
// crystal's rosettes buckle into one another and drift as the waves' phases
// travel.
//
// Coloured as holographic foil: the direction the field slopes picks the hue,
// so every facet catches a different colour the way embossed foil does, with
// a bright sheen where the slope is steep.
//
// ---- audio ------------------------------------------------------------------
//
// HARMONICITY (uHarmony, smoothed over 1 s) chooses the crystal's grain:
// tonal, sustained music keeps it a smooth interference pattern; noisy,
// percussive passages fold it into ridges (1 - |f|) - the same crystal with
// cracked, glassy creases - and the warp follows the same morph. A tonal
// break visibly heals the lattice. The beat envelope (uEvents.x) gently
// tightens the crystal's scale. Brightness follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock and the wave phases. Complexity: uMorph
// adds two more plane waves (seven to nine) past 0.5, and a nest level.
// Chaos: uTurbulence/uWarp raise warp strength and jitter the crystal; uTwist
// twists the plane. Kaleido: when uKaleido is on, uSymmetry (3..11) sets the
// number of waves, which is the crystal's symmetry order. Color: the foil is
// half pal(), half interference ramp. Energy: uIntensity scales the beat
// contraction and how hard harmony re-grains the crystal. Space: uZoom/
// uRotation through lucidView.

#define LAT_MAX_WAVES 11

float quasi(vec2 p, float waves, float phase, out vec2 grad) {
    float s = 0.0;
    grad = vec2(0.0);
    for (int k = 0; k < LAT_MAX_WAVES; k++) {
        float fk = float(k);
        if (fk >= waves) break;
        float a = fk * LUCID_PI / waves;
        vec2 d = vec2(cos(a), sin(a));
        float x = dot(p, d) + phase * (mod(fk, 2.0) * 2.0 - 1.0) * (1.0 + 0.13 * fk);
        s += cos(x);
        grad -= sin(x) * d;
    }
    return s / waves;
    // grad is left unnormalised on purpose: its length is the sheen.
}

void main() {
    vec2 uv = lucidView(false);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float swell = clamp(uSwell, 0.0, 1.5);
    // 1 = smooth crystal, 0 = fully ridged.
    float smoothness = clamp(mix(1.0, clamp(uHarmony, 0.0, 1.0), clamp(uIntensity, 0.0, 1.5)), 0.0, 1.0);
    float beat = clamp(uEvents.x, 0.0, 1.0) * uIntensity;

    float waves = (uKaleido > 0.5 && uSymmetry >= 2.0) ? clamp(floor(uSymmetry + 0.5), 3.0, 11.0)
                                                       : 7.0 + (uMorph >= 0.5 ? 2.0 : 0.0);
    int depth = lucidDetail(3, 2);
    float chaos = lucidChaos();

    vec2 p = uv * (6.0 - 0.6 * beat);
    p += lucidJitter(p, t) * 3.0;
    float phase = t * 0.35 + flow * 0.4;

    vec2 w = vec2(0.0);
    for (int i = 0; i < LUCID_MAX_DEPTH; i++) {
        if (i >= depth) break;
        float lv = float(depth - 1 - i);
        vec2 g;
        vec2 pi = lucidRot(0.4 * lv) * p * exp2(-0.5 * lv) + w;
        quasi(pi, waves, phase * (1.0 - 0.2 * lv), g);
        // Ridged mode folds the warp too: the gradient's sign flips at the creases.
        vec2 ridged = abs(g) - 0.6;
        w = 0.55 * chaos * mix(ridged, g, smoothness);
    }
    vec2 g;
    float f = quasi(p + w, waves, phase, g);
    float field = mix(1.0 - 2.0 * abs(f), f, smoothness);

    float hue = atan(g.y, g.x) / LUCID_TAU + 0.35 * field;
    float slope = length(g) / sqrt(waves);
    vec3 foil = mix(lucidFilm(hue), pal(hue), 0.5);
    vec3 col = foil * (0.18 + 0.55 * smoothstep(-0.4, 0.9, field));
    col += mix(vec3(1.0), pal(hue + 0.5), 0.4) * smoothstep(0.55, 1.1, slope) * 0.35;
    col = lucidCubic(col, 0.6);

    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
