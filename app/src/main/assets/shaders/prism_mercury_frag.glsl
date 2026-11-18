#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_neon
//#include lib_strata
//#include lib_prism

// Prism Mercury: a flight between two seas of liquid chrome, one below and
// one hung above it upside down. Each sea is tiled two units square, and in
// every tile ripples run out from a spring at its middle, folded six ways so
// each ring of ripples is a rosette; the sea above sets its springs between
// those below and its ripples run inward. A line of light rides every crest.
//
// Morph pulls the ripples from round rings into six-pointed stars.
//
// Formula, three seas (lib_strata): 0 the rosettes of ripples, as they
// always were; 0.5 stirred: each tile's ripples run over a domain warped by
// a warp of itself, and a second, finer swell rides the first, so the
// rosettes smear into eddies; 1 a relief of rings: each tile folded by a
// kaleidoscope gStrataIters times, its crests the rims of rings within rings
// at every scale. The far fractal follows: a Mandelbox, a kaleidoscope dust,
// an Apollonian ball, seen between the seas and in the chrome.
//
// Worst case 108 steps, one fold per sea or at most 3 kaleidoscope folds,
// no noise; then the layers.

#define MERC_N 6.0

float mercurySea(vec2 v, float side, float kind, out float crest) {
    vec2 c = v - 2.0 * floor(v * 0.5) - 1.0;
    float r = length(c);
    // Still at the tile's edge, so neighbouring tiles meet without a seam.
    float fade = 1.0 - smoothstep(0.6, 1.0, r);
    if (kind > 1.5) {
        // Rings within rings: the tile folded, each fold's ring a crest.
        float dr = 1.0;
        vec2 q = strataFold2(c * 1.4, 0.0, gStrataIters, vec2(1.2, 0.5), prismRot(0.4 + 0.3 * gPrismForm), dr);
        float ring = abs(length(q) - 0.7) / (1.4 * dr);
        crest = ring + (1.0 - fade);
        return 0.08 * (1.0 - smoothstep(0.0, 0.1, ring)) * fade;
    }
    if (kind > 0.5) {
        // Stirred: the tile's own coordinates warped twice over, periodic with the tile.
        c += 0.18 * sin(PRISM_PI * v.yx + vec2(0.0, 1.7) + uTime * PRISM_RATE(300));
        c += 0.09 * sin(2.0 * PRISM_PI * c.yx + vec2(2.1, 0.4));
    }
    vec2 q = prismFold(c, MERC_N);
    float rr = length(c);
    float s = rr * (1.0 + 0.22 * gPrismForm * cos(MERC_N * prismAngle(q)));
    float ph = 7.0 * s - side * uTime * PRISM_RATE(1400);
    crest = abs(ph - PRISM_TAU * floor(ph / PRISM_TAU + 0.5)) / 7.0 + (1.0 - fade);
    float swell = kind > 0.5 ? 0.035 * cos(2.6 * ph + 1.3) : 0.0;
    return (0.09 * cos(ph) + swell) * fade;
}

vec2 seas(vec3 p, float kind) {
    float c0;
    float c1;
    // The stirred and ringed seas are steeper, so their distance is scaled back further.
    float bound = kind > 1.5 ? 0.55 : (kind > 0.5 ? 0.45 : 0.7);
    float below = (p.y + 1.0 - mercurySea(p.xz, 1.0, kind, c0)) * bound;
    float above = (1.0 - p.y - mercurySea(p.xz + 1.0, -1.0, kind, c1)) * bound;
    return vec2(min(below, above), below < above ? c0 : c1);
}

vec2 prismMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = seas(p, 0.0);
    if (strataUses(1)) b = seas(p, 1.0);
    if (strataUses(2)) c = seas(p, 2.0);
    return strataBlend2(a, b, c);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    strataSetup(vec3(1.0, 0.0, 2.0), 0.0, 0.45, 0.8);
    gPrismStride = 0.8;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 0.9);
}
