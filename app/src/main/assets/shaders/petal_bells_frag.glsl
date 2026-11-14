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

// Petal Bells: bell cups hung from their tops, turned about. Each mouth is
// flared and scalloped into five lobes, every bell turned by the golden angle
// from the last; the lamp hangs inside the largest.
//
// Three formulas (Formula, lib_layers), each a different hanging:
//   0  a nest of five bells one inside another, each the one outside it
//      shrunk by the same share about the same top, so their mouths hang in
//      tiers
//   1  a chandelier: two nested bells in the middle and five smaller ones
//      hung round them on a ring, each tipped out
//   2  a bell tree: a bell with a smaller one inside it, and three more hung
//      from its mouth, a generation down, that grow with the iteration
//      (Detail, k1 TEXTURE, k4 BUILD)
//
// Morph flares the mouths and lengthens the bells. Worst case 96 steps and 4
// sheets, three bells to a point, no noise.

/** Bell k of the nest (0 the outermost; k may be fractional), hung from its top at the origin, opening down. */
vec3 bell(vec3 p, float k) {
    // 0.72 to the k.
    float s = exp(-0.3285 * k);
    vec3 q = p / s;
    float h = max(-q.y, 0.0);
    float phi = petalAtan(q.xz) - k * PETAL_GOLD;
    float mouth = mix(1.4, 1.9, gPetalForm) * (1.0 - 0.07 * (1.0 - cos(5.0 * phi)));
    float flare = smoothstep(0.55 * mouth, mouth, h);
    // Pointed at the top, widening down to the flared lip.
    float rad = 0.75 * h / sqrt(h + 0.2) + mix(0.15, 0.6, gPetalForm) * flare * flare;
    float o = max(h - mouth, q.y);
    return vec3(petalCut((length(q.xz) - rad) * 0.7 * s, o * s), h / mouth, 10.0 * phi / PETAL_TAU);
}

vec3 bellNest(vec3 p) {
    // Which bells lie about this distance from the top: each is 0.72 the one outside it.
    float k = clamp(floor(log(max(length(p), 1.0e-4) / 1.3) / -0.3285), 0.0, 4.0);
    vec3 m = petalNear(bell(p, k), bell(p, min(k + 1.0, 4.0)));
    return petalNear(m, bell(p, max(k - 1.0, 0.0)));
}

vec3 bellChandelier(vec3 p) {
    vec3 m = petalNear(bell(p - vec3(0.0, 0.3, 0.0), 0.6), bell(p - vec3(0.0, 0.3, 0.0), 1.8));
    // Five on a ring, hung lower and tipped out from the middle.
    vec3 q = p + vec3(0.0, 0.35, 0.0);
    q.xz = layerSector(q.xz, 5.0) - vec2(1.75, 0.0);
    q.xy = petalRot(-0.3) * q.xy;
    return petalNear(m, bell(q, 2.4));
}

vec3 bellTree(vec3 p) {
    vec3 m = petalNear(bell(p, 0.3), bell(p, 1.6));
    // A generation down: three bells hung from the mouth, grown in by the iteration.
    vec3 q = p + vec3(0.0, 1.45, 0.0);
    q.xz = layerSector(q.xz, 3.0) - vec2(0.75, 0.0);
    return petalNear(m, bell(q, mix(3.6, 2.2, gLayerIter)));
}

vec3 petalMap(vec3 p) {
    p.xz = petalRot(uTime * PETAL_RATE(55)) * p.xz;
    p.y -= 1.0;
    vec3 m = vec3(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * bellNest(p);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * bellChandelier(p);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * bellTree(p);
    return m;
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.6;
    gPetalLamp = vec3(0.0, -0.1, 0.0);
    gPetalFall = 3.0;
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.4, CAM_SPIN_UNIT, ro, rd);
    fragColor = petalFinish(petalTrace(ro, rd), 1.0);
}
