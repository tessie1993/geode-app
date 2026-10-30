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

// Arbor Grove: one tree on a hill against a low dusk sun, a smaller one on
// the far ridge, the first stars coming out above. Flat 2D: no march, so it
// ignores uSteps and costs two fold walks a pixel. It is the family's free
// style, so it is the plainest statement of the idea: a silhouette that the
// music blows about.
//
// The tree is arborTree2 with slightly lopsided forks and a little hashed
// irregularity per branch, so it reads as grown rather than drawn by ruler.
// The sky colours lean toward the user's palette without leaving dusk.
//
// ---- audio ------------------------------------------------------------------
//
// Bass is the WIND. uBassSmooth (slew-limited, so no single frame can jerk a
// branch) sets the lean every fork adds, and because the lean compounds down
// the levels the trunk barely moves while the crown bows and the twigs whip.
// A slow gust oscillator rides on top so a held bass note still sways. Air
// (uBandsHi.z) glints on the outermost twig tips on the sun side, small
// points, never the frame. The sun's halo widens with uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the gust, star twinkle and every clock. Complexity:
// uMorph adds up to one level to each tree (arbLevels2). Chaos: uTurbulence and
// uWarp add per-branch jitter and a noisy flutter to the wind; uTwist adds a
// standing spiral curl. Colour: sky, sun and rim come from pal(), with a fixed
// dusk tint underneath. Energy: uIntensity scales the bass lean and the air
// glints. Space: the view zoom and pan.

#define GR_LEVELS 9.0
#define GR_FAR_LEVELS 7.0

float groundY(float x) {
    return -0.80 + 0.07 * cos(x * 1.3 + 0.4);
}

float ridgeY(float x) {
    return -0.60 + 0.05 * sin(x * 2.1 + 1.0) + 0.025 * sin(x * 5.3 + 0.3);
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

    // ---- sky: the palette's warm end at the horizon, its far end overhead ---
    vec3 horizon = mix(pal(0.02) * 1.15, vec3(1.0, 0.52, 0.24), 0.35);
    vec3 dusk = mix(pal(0.30) * 0.55, vec3(0.50, 0.20, 0.32), 0.35);
    vec3 night = pal(0.62) * 0.05 + vec3(0.02, 0.018, 0.06);
    vec3 col = mix(horizon, dusk, smoothstep(-0.50, 0.15, uv.y));
    col = mix(col, night, smoothstep(0.05, 1.05, uv.y));

    vec2 sunP = vec2(0.22, -0.34);
    float sd = length(uv - sunP);
    vec3 sunCol = mix(pal(0.05), vec3(1.0, 0.85, 0.6), 0.55);
    col += sunCol * (1.0 - smoothstep(0.15 - px, 0.15 + px, sd)) * 0.8;
    col += horizon * exp(-sd * (3.6 - 0.9 * swell)) * (0.30 + 0.12 * swell);

    vec2 sg = uv * 38.0;
    float sh = hash13(vec3(floor(sg), 7.0));
    float star = step(0.986, sh) * smoothstep(0.12, 0.0, length(fract(sg) - 0.5));
    star *= smoothstep(0.15, 0.8, uv.y) * (0.6 + 0.4 * sin(t * (1.0 + 3.0 * sh) + sh * 40.0));
    col += vec3(0.85, 0.85, 1.0) * star * 0.7;

    // ---- the wind -----------------------------------------------------------
    float gust = 0.55 + 0.45 * sin(t * 1.25) * sin(t * 0.53 + 1.0);
    float flutter = chaos * 0.025 * sin(t * 4.1) * sin(t * 2.7 + 1.3);
    float lean = (0.006 + 0.04 * bass) * (gust - 0.35) + 0.010 * sin(t * 0.37) + flutter + 0.08 * uTwist;
    float jitter = 0.20 + 0.45 * chaos;

    // ---- far ridge and its tree, hazy --------------------------------------
    vec3 haze = mix(col, pal(0.45) * 0.18 + vec3(0.05, 0.02, 0.04), 0.62);
    float ridge = arbInk(uv.y - ridgeY(uv.x), px);
    col = mix(col, haze, ridge);

    const float farScale = 0.30;
    vec2 farBase = vec2(0.40, ridgeY(0.40) - 0.02);
    ArborForm2 farF = ArborForm2(vec2(0.50, 0.44), vec2(0.70, 0.66), lean * 0.6, jitter + 0.1, 0.40, 0.09, 0.70);
    float farD = arborTree2((uv - farBase) / farScale, farF, arbLevels2(GR_FAR_LEVELS, 1.0), 3.0) * farScale;
    col = mix(col, haze * 0.8, arbInk(farD, px));

    // ---- near hill and the tree ---------------------------------------------
    vec3 ink = vec3(0.025, 0.018, 0.04);
    float ground = arbInk(uv.y - groundY(uv.x), px);
    col = mix(col, ink, ground);

    float levels = arbLevels2(GR_LEVELS, 1.0);
    vec2 base = vec2(-0.12, groundY(-0.12) - 0.02);
    ArborForm2 f = ArborForm2(vec2(0.46, 0.42), vec2(0.72, 0.70), lean, jitter, 0.34, 0.085, 0.72);
    float d = arborTree2(uv - base, f, levels, 11.0);
    float tip = gArbTip;
    float lvl = gArbLevel;
    col = mix(col, ink, arbInk(d, px));

    // Rim light on the edge that faces the sun, strongest close to it.
    float sunSide = smoothstep(1.4, 0.15, sd);
    float rim = (1.0 - smoothstep(0.0, 3.5 * px, abs(d))) * sunSide * (1.0 - ground);
    col += horizon * rim * 0.45;
    // Air glints on the outermost tips, sun side only.
    float glint = (1.0 - smoothstep(0.0, 0.012, tip)) * step(levels - 2.0, lvl) * sunSide;
    col += sunCol * glint * air * 0.6;

    col *= 0.9 + 0.12 * swell;
    col *= 0.78 + 0.22 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
