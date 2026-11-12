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
//#include lib_petal

// Petal Rose: the family's opener. A flight into rose after rose along the
// way, each a nest of cupped layers opening toward the eye, five petals to a
// layer and every layer turned by the golden angle from the one inside it; a
// lamp at each heart shines out through the layers.
//
// Morph opens the roses: from tight buds whose petals close over the heart to
// full blooms whose outer layers flare back. Worst case 96 steps and 4
// sheets, two layers to a point, no noise.

/** Layer k (0 the innermost) at spherical angles about the rose's heart: rho out, psi from the back of the cup, phi round it. */
vec3 roseLayer(float rho, float psi, float phi, float k) {
    float outer = k / 6.0;
    float lobe = 0.5 + 0.5 * cos(5.0 * phi - k * PETAL_GOLD);
    // How far round from the back of the cup the petals reach, notched between petals.
    float edge = mix(mix(2.5, 2.0, outer), mix(1.9, 1.25, outer), gPetalForm);
    edge *= 1.0 - 0.3 * (1.0 - lobe) * (1.0 - lobe);
    float reach = psi / edge;
    float rad = 0.32 * exp(0.3221 * k) * (1.0 + 0.35 * gPetalForm * outer * reach * reach);
    return vec3(petalCut(rho - rad, (psi - edge) * rho), reach, 15.0 * phi / PETAL_TAU + 0.5 * k);
}

vec3 petalMap(vec3 p) {
    vec3 q = vec3(p.xy, p.z - 8.0 * floor(p.z / 8.0) - 4.0);
    float rho = max(length(q), 1.0e-4);
    float psi = acos(clamp(q.z / rho, -1.0, 1.0));
    float phi = petalAtan(q.xy);
    // The layers grow by 1.38 each; the two about rho.
    float k = clamp(floor(log(rho / 0.32) / 0.3221), 0.0, 5.0);
    vec3 m = petalNear(roseLayer(rho, psi, phi, k), roseLayer(rho, psi, phi, k + 1.0));
    // The next rose along is at least a unit past this cell's end.
    return petalHold(m, 5.0 - abs(q.z));
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.6;
    gPetalLamp = vec3(0.0, 0.0, 4.0);
    gPetalGap = 8.0;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = petalFinish(petalTrace(petalEye(ro, 0.6), rd), 1.0);
}
