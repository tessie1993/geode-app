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

// Prism Jewel: a five-pointed star cut as a chrome gem, turned about. Every
// facet runs from a point and a notch of the star at its waist to the apex
// above or below, one facet mirrored twenty times; a scalloped hoop rings it
// at the waist and turns the other way. Lines of light run up every ridge and
// down every valley, round the waist and round the hoop.
//
// Morph cuts the notches deeper and draws the apexes out, from a stout
// pentagon to a spiked star.
//
// Formula, three gems (lib_strata): 0 the one star, as it always was; 0.5
// the gem pierced by a Menger sponge, square shafts within square shafts
// through the chrome, so its insides mirror each other, gStrataIters folds;
// 1 a compound: the star and copies of it turned a quarter about the other
// two axes, interpenetrating, as many as gStrataIters, the last growing in.
// The far fractal follows: a kaleidoscope dust, an Apollonian ball, a Menger
// cube.
//
// Worst case 108 steps, at most three gems or 3 sponge folds a step, one fold
// each, no noise; then the layers.

/** The star gem at s (already turned), `grow` its size (1 the full gem): x the distance, y the ribbon's. */
vec2 gemAt(vec3 s, float grow) {
    s /= grow;
    vec2 q = prismFold(s.xz, JEWEL_N);
    float y = abs(s.y);
    float h = PRISM_PI / JEWEL_N;
    float tip = 2.0;
    float notch = tip * mix(0.72, 0.38, gPrismForm);
    float apex = mix(1.3, 2.1, gPrismForm);
    // The facet through the point, the notch and the apex, its normal facing out.
    vec3 n = normalize(vec3(notch * sin(h) * apex, notch * sin(h) * tip, (tip - notch * cos(h)) * apex));
    float gem = dot(vec3(q.x - tip, y, q.y), n);
    return vec2(gem, min(min(q.y, prismEdge(q, JEWEL_N)), y)) * grow;
}

vec2 prismMap(vec3 p) {
    vec3 s = p;
    s.xz = prismRot(uTime * PRISM_RATE(40)) * s.xz;
    vec2 m = gemAt(s, 1.0);
    vec2 a = m;
    vec2 b = m;
    vec2 c = m;
    if (strataUses(1)) {
        float holes = strataHoles(s, 1.8, gStrataIters);
        b = vec2(max(m.x, -holes), min(m.y, abs(holes)));
    }
    if (strataUses(2)) {
        // The compound: the star turned a quarter about x, then about z, each growing in.
        float g1 = clamp(gStrataIters, 0.0, 1.0);
        float g2 = clamp(gStrataIters - 1.0, 0.0, 1.0);
        vec2 t = g1 > 0.0 ? gemAt(vec3(s.x, -s.z, s.y), g1) : vec2(1.0e3);
        c = t.x < c.x ? t : c;
        t = g2 > 0.0 ? gemAt(vec3(s.y, -s.x, s.z), g2) : vec2(1.0e3);
        c = t.x < c.x ? t : c;
    }
    m = strataBlend2(a, b, c);
    vec3 g = p;
    g.xz = prismRot(-uTime * PRISM_RATE(30)) * g.xz;
    vec2 gq = prismFold(g.xz, 2.0 * JEWEL_N);
    float gr = 3.0 + 0.22 * cos(2.0 * JEWEL_N * prismAngle(gq));
    float hoop = length(vec2(length(g.xz) - gr, g.y)) - 0.06;
    return m.x < hoop ? m : vec2(hoop, abs(g.y));
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    strataSetup(vec3(0.0, 2.0, 3.0), 0.0, 0.45, 0.7);
    gPrismStride = 0.6;
    vec3 ro;
    vec3 rd;
    camAround(uv, 8.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = prismFinish(prismTrace(ro, rd), 1.2);
}
