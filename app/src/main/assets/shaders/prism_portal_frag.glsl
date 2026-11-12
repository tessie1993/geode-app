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

// Prism Portal: the family's opener. A flight through a string of star
// gates, one every four units of the way: a chrome hoop bent into a
// six-pointed star, a smaller star inside it set half a point round, the two
// turning opposite ways, and between the gates a wide star further out. A
// line of light runs round each star's rims and rings every point and notch,
// where the hoop crosses a mirror.
//
// Morph draws the points out, from soft hexagons to sharp stars. Worst case
// 108 steps, one fold per star, no noise.

#define PORTAL_N 6.0

float portalStar(vec2 v, float tip, float notch, float tube, float zl, out float seam) {
    vec2 q = prismFold(v, PORTAL_N);
    float h = PRISM_PI / PORTAL_N;
    float d2 = prismSeg(q, vec2(tip, 0.0), notch * vec2(cos(h), sin(h)));
    seam = min(abs(zl), min(q.y, prismEdge(q, PORTAL_N)));
    return length(vec2(d2, zl)) - tube;
}

vec2 prismMap(vec3 p) {
    float zl = p.z - 4.0 * floor(p.z * 0.25 + 0.5);
    float zm = p.z - 4.0 * floor(p.z * 0.25) - 2.0;
    float spin = uTime * PRISM_RATE(60);
    float sharp = mix(0.84, 0.48, gPrismForm);
    float s0;
    float s1;
    float s2;
    float d0 = portalStar(prismRot(spin) * p.xy, 2.3, 2.3 * sharp, 0.07, zl, s0);
    float d1 = portalStar(prismRot(PRISM_PI / PORTAL_N - spin) * p.xy, 1.25, 1.25 * sharp, 0.05, zl, s1);
    float d2 = portalStar(prismRot(0.5 * spin) * p.xy, 3.6, 3.6 * sharp, 0.09, zm, s2);
    float d = min(d0, min(d1, d2));
    float seam = d0 < d1 ? s0 : s1;
    seam = d2 < min(d0, d1) ? s2 : seam;
    return vec2(d, seam);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.1);
}
