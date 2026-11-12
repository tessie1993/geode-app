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

// Petal Stair: a spiral stair of fronds about one slender stem, turned about,
// the lamp in the stem's middle. Each frond springs from the stem a step above
// the last and turned from it by the golden angle, a long blade arching out
// and down, troughed along its midrib.
//
// Morph spreads the fronds, from short blades folded up along their midribs
// to long flat ones. Worst case 96 steps and 4 sheets, three fronds to a
// point, no noise.

/** Frond n (-9..9; any other is cut away), its foot on the stem at height 0.32 n. */
vec3 stairFrond(vec3 p, float n) {
    vec3 q = vec3(p.x, p.y - 0.32 * n, p.z);
    q.xz = petalRot(n * PETAL_GOLD) * q.xz;
    // q.x along the blade, q.z across it, q.y up.
    float len = mix(1.3, 2.0, gPetalForm);
    float s = q.x;
    float fold = mix(1.4, 0.25, gPetalForm);
    float slope = 0.55 - 0.64 * s;
    float f = (q.y - 0.55 * s + 0.32 * s * s - fold * q.z * q.z) / sqrt(1.0 + slope * slope + 4.0 * fold * fold * q.z * q.z);
    float wide = 0.3 * sin(3.1415927 * clamp(s / len, 0.0, 1.0)) + 0.02;
    float o = max(abs(q.z) - wide, max(max(0.08 - s, s - len), abs(n) - 9.0));
    return vec3(petalCut(f, o), max(abs(q.z) / wide, s / len), 2.0 * q.z / wide);
}

vec3 petalMap(vec3 p) {
    p.xz = petalRot(uTime * PETAL_RATE(45)) * p.xz;
    // A frond rises or falls less than one step from its foot, so three steps reach any point.
    float n = clamp(floor(p.y / 0.32 + 0.5), -9.0, 9.0);
    vec3 m = petalNear(stairFrond(p, n), stairFrond(p, n - 1.0));
    m = petalNear(m, stairFrond(p, n + 1.0));
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
