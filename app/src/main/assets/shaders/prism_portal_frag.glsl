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

// Prism Portal: the family's opener. A flight through a string of star
// gates, one every four units of the way: a chrome hoop bent into a
// six-pointed star, a smaller star inside it set half a point round, the two
// turning opposite ways, and between the gates a wide star further out. A
// line of light runs round each star's rims and rings every point and notch,
// where the hoop crosses a mirror.
//
// Morph draws the points out, from soft hexagons to sharp stars.
//
// Formula, three gates (lib_strata): 0 the star gates, as they always were;
// 0.5 snowflakes: on every point of the gate a star 0.4 its size, and on
// every point of those another, as many as gStrataIters, the last growing
// in; 1 carpets: the gate a square plate cut as a Menger carpet round the
// open way (the carpet's middle hole), squares within squares, the wide star
// between the gates kept. The far fractal follows: a kaleidoscope dust, an
// Apollonian ball, a Menger cube.
//
// Worst case 108 steps, at most three stars a gate and a fold each, or 3
// carpet folds, no noise; then the layers.

#define PORTAL_N 6.0

float portalStar(vec2 v, float tip, float notch, float tube, float zl, out float seam) {
    vec2 q = prismFold(v, PORTAL_N);
    float h = PRISM_PI / PORTAL_N;
    float d2 = prismSeg(q, vec2(tip, 0.0), notch * vec2(cos(h), sin(h)));
    seam = min(abs(zl), min(q.y, prismEdge(q, PORTAL_N)));
    return length(vec2(d2, zl)) - tube;
}

/** The gate's star at v with stars on its points, `nest` deep: its distance, `seam` the ribbon's. */
float snowStar(vec2 v, float tip, float sharp, float tube, float zl, float nest, out float seam) {
    float d = portalStar(v, tip, tip * sharp, tube, zl, seam);
    for (int i = 0; i < 3; i++) {
        float f = clamp(nest - float(i), 0.0, 1.0);
        if (f <= 0.0) break;
        // The next star sits on this one's point, 0.4 the size, turned half a point.
        vec2 q = prismFold(v, PORTAL_N);
        v = prismRot(PRISM_PI / PORTAL_N) * (q - vec2(tip, 0.0));
        tip *= 0.4 * f;
        tube *= 0.6;
        float s;
        float e = portalStar(v, tip, tip * sharp, tube, zl, s);
        seam = e < d ? s : seam;
        d = min(d, e);
    }
    return d;
}

vec2 gates(vec3 p, float nest, bool carpet) {
    float zl = p.z - 4.0 * floor(p.z * 0.25 + 0.5);
    float zm = p.z - 4.0 * floor(p.z * 0.25) - 2.0;
    float spin = uTime * PRISM_RATE(60);
    float sharp = mix(0.84, 0.48, gPrismForm);
    float s0;
    float s1;
    float s2;
    float d0;
    float d1;
    if (carpet) {
        // The plate lies in the sponge's top layer, where its cut is a carpet.
        vec2 v = prismRot(spin) * p.xy;
        float sponge = strataSponge(vec3(v, zl + 2.4), 3.6, gStrataIters);
        d0 = max(abs(zl) - 0.05, sponge);
        s0 = min(abs(sponge), abs(zl));
        d1 = 1.0e3;
        s1 = 1.0e3;
    } else {
        d0 = snowStar(prismRot(spin) * p.xy, 2.3, sharp, 0.07, zl, nest, s0);
        d1 = snowStar(prismRot(PRISM_PI / PORTAL_N - spin) * p.xy, 1.25, sharp, 0.05, zl, nest, s1);
    }
    float d2 = portalStar(prismRot(0.5 * spin) * p.xy, 3.6, 3.6 * sharp, 0.09, zm, s2);
    float d = min(d0, min(d1, d2));
    float seam = d0 < d1 ? s0 : s1;
    seam = d2 < min(d0, d1) ? s2 : seam;
    return vec2(d, seam);
}

vec2 prismMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = gates(p, 0.0, false);
    if (strataUses(1)) b = gates(p, gStrataIters, false);
    if (strataUses(2)) c = gates(p, 0.0, true);
    return strataBlend2(a, b, c);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    strataSetup(vec3(0.0, 2.0, 3.0), 1.0, 0.45, 0.7);
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.1);
}
