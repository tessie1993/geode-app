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

// Petal Stair: fronds about one slender stem, turned about, the lamp in the
// stem's middle: long blades arching out and down, troughed along their
// midribs.
//
// Three formulas (Formula, lib_layers), each a different phyllotaxis:
//   0  a spiral stair: each frond a step above the last, turned from it by
//      the golden angle
//   1  a fan: the fronds in two ranks, each turned half round from the last
//      and a little more, so the plant spreads flat and slowly twists
//   2  whorls: four fronds to a tier, a tier every two steps, each tier turned
//      an eighth from the one below, like a horsetail
//
// Morph spreads the fronds, from short blades folded up along their midribs
// to long flat ones. Worst case 96 steps and 4 sheets, three fronds to a
// point, no noise.

/** A frond in its own frame: q.x along the blade from its foot on the stem, q.z across it, q.y up; `n` its step (cut away past 9). */
vec3 stairBlade(vec3 q, float n) {
    float len = mix(1.3, 2.0, gPetalForm);
    float s = q.x;
    float fold = mix(1.4, 0.25, gPetalForm);
    float slope = 0.55 - 0.64 * s;
    float f = (q.y - 0.55 * s + 0.32 * s * s - fold * q.z * q.z) / sqrt(1.0 + slope * slope + 4.0 * fold * fold * q.z * q.z);
    float wide = 0.3 * sin(3.1415927 * clamp(s / len, 0.0, 1.0)) + 0.02;
    float o = max(abs(q.z) - wide, max(max(0.08 - s, s - len), abs(n) - 9.0));
    return vec3(petalCut(f, o), max(abs(q.z) / wide, s / len), 2.0 * q.z / wide);
}

/** Frond n, its foot on the stem at height 0.32 n, turned `turn` per step. */
vec3 stairFrond(vec3 p, float n, float turn) {
    vec3 q = vec3(p.x, p.y - 0.32 * n, p.z);
    q.xz = petalRot(n * turn) * q.xz;
    return stairBlade(q, n);
}

vec3 stairSpiral(vec3 p, float turn) {
    // A frond rises or falls less than one step from its foot, so three steps reach any point.
    float n = clamp(floor(p.y / 0.32 + 0.5), -9.0, 9.0);
    vec3 m = petalNear(stairFrond(p, n, turn), stairFrond(p, n - 1.0, turn));
    return petalNear(m, stairFrond(p, n + 1.0, turn));
}

/** Tier w of four fronds, its foot at height 0.64 w. */
vec3 stairTier(vec3 p, float w) {
    vec3 q = vec3(p.x, p.y - 0.64 * w, p.z);
    q.xz = petalRot(w * 0.7853982) * q.xz;
    q.xz = layerSector(q.xz, 4.0);
    return stairBlade(q, 2.0 * w);
}

vec3 stairWhorls(vec3 p) {
    float w = clamp(floor(p.y / 0.64 + 0.5), -4.0, 4.0);
    vec3 m = petalNear(stairTier(p, w), stairTier(p, w - 1.0));
    m = petalNear(m, stairTier(p, w + 1.0));
    // Four fronds folded into one wedge: a neighbour's blade lies past the wedge's sides.
    return petalHold(m, length(p.xz) * 0.7071068 - abs(layerSector(p.xz, 4.0).y) + 0.1);
}

vec3 petalMap(vec3 p) {
    p.xz = petalRot(uTime * PETAL_RATE(45)) * p.xz;
    vec3 m = vec3(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * stairSpiral(p, PETAL_GOLD);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * stairSpiral(p, 3.1415927 + 0.21);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * stairWhorls(p);
    vec3 stem = vec3(petalCut(length(p.xz) - 0.06, abs(p.y) - 3.2), 0.0, 0.0);
    return petalNear(m, stem);
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.65;
    gPetalFall = 2.0;
    vec3 ro;
    vec3 rd;
    camAround(uv, 5.2, CAM_SPIN_UNIT, ro, rd);
    fragColor = petalFinish(petalTrace(ro, rd), 1.0);
}
