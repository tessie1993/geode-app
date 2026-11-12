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

// Prism Jewel: a five-pointed star cut as a chrome gem, turned about. Every
// facet runs from a point and a notch of the star at its waist to the apex
// above or below, one facet mirrored twenty times; a scalloped hoop rings it
// at the waist and turns the other way. Lines of light run up every ridge and
// down every valley, round the waist and round the hoop.
//
// Morph cuts the notches deeper and draws the apexes out, from a stout
// pentagon to a spiked star. Worst case 108 steps, one fold, no noise.

#define JEWEL_N 5.0

vec2 prismMap(vec3 p) {
    vec3 s = p;
    s.xz = prismRot(uTime * PRISM_RATE(40)) * s.xz;
    vec2 q = prismFold(s.xz, JEWEL_N);
    float y = abs(s.y);
    float h = PRISM_PI / JEWEL_N;
    float tip = 2.0;
    float notch = tip * mix(0.72, 0.38, gPrismForm);
    float apex = mix(1.3, 2.1, gPrismForm);
    // The facet through the point, the notch and the apex, its normal facing out.
    vec3 n = normalize(vec3(notch * sin(h) * apex, notch * sin(h) * tip, (tip - notch * cos(h)) * apex));
    float gem = dot(vec3(q.x - tip, y, q.y), n);
    vec3 g = p;
    g.xz = prismRot(-uTime * PRISM_RATE(30)) * g.xz;
    vec2 gq = prismFold(g.xz, 2.0 * JEWEL_N);
    float gr = 3.0 + 0.22 * cos(2.0 * JEWEL_N * prismAngle(gq));
    float hoop = length(vec2(length(g.xz) - gr, g.y)) - 0.06;
    float seam = gem < hoop ? min(min(q.y, prismEdge(q, JEWEL_N)), y) : abs(g.y);
    return vec2(min(gem, hoop), seam);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    gPrismStride = 0.6;
    vec3 ro;
    vec3 rd;
    camAround(uv, 8.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = prismFinish(prismTrace(ro, rd), 1.2);
}
