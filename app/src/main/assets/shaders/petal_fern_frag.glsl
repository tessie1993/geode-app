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

// Petal Fern: fiddleheads, turned about: fronds wound in a logarithmic
// spiral, narrow at the tight heart of the coil and broad at its open end,
// their edges lobed into leaflets and ruled along their length.
//
// Three formulas (Formula, lib_layers), each a different growth:
//   0  three fiddleheads, two smaller beside the first, each turned by the
//      golden angle from the last
//   1  a whorl: the coil swept round the upright, so it winds into a bowl of
//      seven spiral blades, each a slice of the same coil
//   2  a fractal clump: one fiddlehead folded kaleidoscopically into copies
//      of itself, each smaller and turned, more of them as the iteration
//      grows (Detail, k1 TEXTURE, k4 BUILD)
//
// Morph unfurls them, from tight coils of six turns to loose sweeps of two.
// Worst case 96 steps and 4 sheets, three fronds to a point, no noise.

/** One frond, its coil in the xy plane about the origin, out to radius 1.9. */
vec3 frond(vec3 p) {
    float r = max(length(p.xy), 1.0e-3);
    float th = petalAtan(p.xy) / PETAL_TAU;
    // How much the coil grows (log) in one turn.
    float b = mix(0.5, 1.4, gPetalForm);
    float grow = exp(b);
    // The winding just inside r, or the one outside it where that is nearer.
    float w = th + floor(log(r) / b - th);
    float rk = exp(b * w);
    if (rk * grow - r < r - rk) {
        w += 1.0;
        rk *= grow;
    }
    float wide = rk * 0.32 * (0.6 + 0.4 * abs(sin(PETAL_TAU * 4.5 * w)));
    // The frond runs from radius 0.1 at the heart of the coil to 1.9 at its open end.
    float ends = max(-2.3026 / b - w, w - 0.6419 / b) * rk * PETAL_TAU;
    float o = max(abs(p.z) - wide, ends);
    return vec3(petalCut(r - rk, o), abs(p.z) / wide, 2.0 * p.z / wide);
}

vec3 fernThree(vec3 p) {
    vec3 m = frond(p - vec3(0.0, 0.35, 0.0));
    // Two smaller ones beside it, each turned by the golden angle from the last.
    vec3 q = p - vec3(1.3, -0.9, 0.6);
    q.xz = petalRot(PETAL_GOLD) * q.xz;
    vec3 f = frond(q * 1.5);
    f.x /= 1.5;
    m = petalNear(m, f);
    q = p - vec3(-1.2, -1.1, -0.7);
    q.xz = petalRot(2.0 * PETAL_GOLD) * q.xz;
    f = frond(q * 2.0);
    f.x *= 0.5;
    return petalNear(m, f);
}

vec3 fernWhorl(vec3 p) {
    // The coil in the plane through the upright, swept round it; seven blades, each a sector wide.
    vec2 s = layerSector(p.xz, 7.0);
    vec3 m = frond(vec3(s.x, p.y + 0.2, s.y * 1.6));
    // The sweep stretches space across the blades, which bounds the step.
    m.x *= 0.7;
    return m;
}

vec3 fernClump(vec3 p) {
    float scale;
    vec3 q = layerKifs(p - vec3(0.0, 0.2, 0.0), layerIters(0.8, 2.6), vec3(0.75, 0.45, 0.6), 0.55, 1.55, scale);
    vec3 f = frond(q * 1.25);
    f.x /= 1.25 * scale;
    return f;
}

vec3 petalMap(vec3 p) {
    p.xz = petalRot(uTime * PETAL_RATE(50)) * p.xz;
    vec3 m = vec3(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * fernThree(p);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * fernWhorl(p);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * fernClump(p);
    return m;
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.6;
    gPetalLamp = vec3(0.0, 0.35, 0.0);
    vec3 ro;
    vec3 rd;
    camAround(uv, 5.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = petalFinish(petalTrace(ro, rd), 1.1);
}
