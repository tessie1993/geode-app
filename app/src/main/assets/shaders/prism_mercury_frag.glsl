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
//#include lib_prism

// Prism Mercury: a flight between two seas of liquid chrome, one below and
// one hung above it upside down. Each sea is tiled two units square, and in
// every tile ripples run out from a spring at its middle, folded six ways so
// each ring of ripples is a rosette; the sea above sets its springs between
// those below and its ripples run inward. A line of light rides every crest.
//
// Morph pulls the ripples from round rings into six-pointed stars. Worst case
// 108 steps, one fold per sea, no noise.

#define MERC_N 6.0

float mercurySea(vec2 v, float side, out float crest) {
    vec2 c = v - 2.0 * floor(v * 0.5) - 1.0;
    vec2 q = prismFold(c, MERC_N);
    float r = length(c);
    float s = r * (1.0 + 0.22 * gPrismForm * cos(MERC_N * prismAngle(q)));
    float ph = 7.0 * s - side * uTime * PRISM_RATE(1400);
    // Still at the tile's edge, so neighbouring tiles meet without a seam.
    float fade = 1.0 - smoothstep(0.6, 1.0, r);
    crest = abs(ph - PRISM_TAU * floor(ph / PRISM_TAU + 0.5)) / 7.0 + (1.0 - fade);
    return 0.09 * cos(ph) * fade;
}

vec2 prismMap(vec3 p) {
    float c0;
    float c1;
    float below = (p.y + 1.0 - mercurySea(p.xz, 1.0, c0)) * 0.7;
    float above = (1.0 - p.y - mercurySea(p.xz + 1.0, -1.0, c1)) * 0.7;
    return vec2(min(below, above), below < above ? c0 : c1);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    gPrismStride = 0.8;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 0.9);
}
