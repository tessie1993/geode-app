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
// way, each a nest of cupped petals opening toward the eye, five to a layer
// and every layer turned by the golden angle from the one inside it; a lamp
// at each heart shines out through them.
//
// Three formulas (Formula, lib_layers), each a different bloom:
//   0  nested layers, each 1.38 times the one inside it
//   1  a camellia: one petal sheet wound out from the heart in a spiral, a
//      layer's growth to every turn, so the bloom is a single scroll
//   2  a wall of roses: five smaller roses round the way at every station,
//      their cups turned in toward it, the way running between them
//
// Morph opens the roses: from tight buds whose petals close over the heart to
// full blooms whose outer layers flare back. Worst case 96 steps and 4
// sheets, two layers to a point, no noise.

/** Layer k (0 the innermost; k may be fractional) at spherical angles about the rose's heart: rho out, psi from the back of the cup, phi round it. */
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

/** A rose about its heart at q, its cup's back toward +z; `top` its outermost layer. */
vec3 roseAt(vec3 q, bool scroll, float top) {
    float rho = max(length(q), 1.0e-4);
    float psi = acos(clamp(q.z / rho, -1.0, 1.0));
    float phi = petalAtan(q.xy);
    if (scroll) {
        // One sheet, a layer's growth to a turn: the turn about rho, and the one outside it.
        float w = phi / PETAL_TAU;
        float k = clamp(floor(log(rho / 0.32) / 0.3221 - w), -1.0, top - 1.0);
        return petalNear(roseLayer(rho, psi, phi, max(k + w, 0.0)), roseLayer(rho, psi, phi, min(k + 1.0 + w, top)));
    }
    // The layers grow by 1.38 each; the two about rho.
    float k = clamp(floor(log(rho / 0.32) / 0.3221), 0.0, top - 1.0);
    return petalNear(roseLayer(rho, psi, phi, k), roseLayer(rho, psi, phi, k + 1.0));
}

vec3 roseOne(vec3 p, bool scroll) {
    vec3 q = vec3(p.xy, p.z - 8.0 * floor(p.z / 8.0) - 4.0);
    // The next rose along is at least a unit past this cell's end.
    return petalHold(roseAt(q, scroll, 6.0), 5.0 - abs(q.z));
}

vec3 roseWall(vec3 p) {
    // Five round the way, a station every 4 units; each rose's back toward the wall, its cup facing in.
    vec2 s = layerSector(p.xy, 5.0);
    float zc = p.z - 4.0 * floor(p.z / 4.0) - 2.0;
    vec3 q = vec3(s.y, zc, s.x - 2.3);
    q *= 1.35;
    vec3 m = roseAt(q, false, 4.0);
    m.x /= 1.35;
    // Neighbours lie past this sector's sides and this station's ends.
    return petalHold(m, min(2.0 - abs(zc), length(p.xy) * 0.5878 - abs(s.y)) + 0.3);
}

vec3 petalMap(vec3 p) {
    vec3 m = vec3(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * roseOne(p, false);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * roseOne(p, true);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * roseWall(p);
    return m;
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
