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
//#include lib_loom

// Loom Braid: the family's opener. A flight down braided cord: every cord laid
// over the next one it crosses and under the one after, each spun of three
// plies.
//
// Three formulas (Formula, lib_layers), each a different braided structure:
//   0  two sleeves, one inside the other, each wound one way round the way
//      ahead and the other way back
//   1  four braided cables round the way, twisting about it once in every
//      LOOM_PERIOD, inside a sparse outer sleeve
//   2  a star duct: the sleeve's radius swings in five lobes that turn
//      along the way, and a second, finer sleeve folds through the lobes
//      at a radius that grows with the iteration (Detail, k1, k4)
//
// Morph pulls the braid tight: the cords swell until they press together and
// the over and under deepens. Worst case 90 steps, no noise.

/** One braided sleeve of radius `big`, `cords` cords each way: once round in every LOOM_PERIOD. */
vec3 braidSleeve(float r, float th, float z, float big, float cords, float rad, float hue) {
    float wind = LOOM_TAU / LOOM_PERIOD;
    float cell = LOOM_TAU / cords;
    // Where a cord of one hand crosses one of the other, cos(cords * th) is +1 or -1 in turn: over, then under.
    float lift = (rad + 0.02) * cos(cords * th);
    float across = r / sqrt(1.0 + r * r * wind * wind);
    float u = th - wind * z;
    float v = th + wind * z;
    float iu = floor(u / cell + 0.5);
    float iv = floor(v / cell + 0.5);
    vec3 a = loomYarn(vec2(across * (u - cell * iu), r - big - lift), rad, 3.0, 0.5 * z, hue + 0.12 * mod(iu, 3.0));
    vec3 b = loomYarn(vec2(across * (v - cell * iv), r - big + lift), rad, 3.0, -0.5 * z, hue + 0.5 + 0.12 * mod(iv, 3.0));
    return loomNearer(a, b);
}

vec4 braidSleeves(vec3 p, float rad) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    vec3 s = loomNearer(braidSleeve(r, th, p.z, 1.5, 12.0, rad, 0.0), braidSleeve(r, th, p.z, 2.9, 18.0, 1.3 * rad, 0.25));
    return vec4(s.x, s.y, 8.0, s.z);
}

vec4 braidCables(vec3 p, float rad) {
    // The four cables turn about the way once in a period, so the world still repeats.
    vec2 c = loomRot(p.z * (LOOM_TAU / LOOM_PERIOD)) * p.xy;
    vec2 q = layerSector(c, 4.0) - vec2(1.75, 0.0);
    vec3 s = braidSleeve(length(q), loomAngle(q), p.z, 0.62, 8.0, 0.75 * rad, 0.1);
    float r = length(p.xy);
    s = loomNearer(s, braidSleeve(r, loomAngle(p.xy), p.z, 3.3, 10.0, 0.8 * rad, 0.55));
    return vec4(s.x, s.y, 8.0, s.z);
}

vec4 braidStar(vec3 p, float rad) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    float zc = p.z * (LOOM_TAU / LOOM_PERIOD);
    // Five lobes that turn along the way; the radius's slope round the way bounds the step.
    float lobe = 0.32 * cos(5.0 * th + 2.0 * zc);
    float big = 1.7 * (1.0 + lobe);
    float slope = 1.0 / sqrt(1.0 + pow(1.7 * 0.32 * 5.0 / max(r, 0.6), 2.0));
    vec3 s = braidSleeve(r, th, p.z, big, 15.0, rad, 0.05);
    s.x *= slope;
    // The finer sleeve folds through the lobes, further out as the iteration grows.
    float inner = mix(2.1, 2.9, gLayerIter) * (1.0 - 0.6 * lobe);
    vec3 t = braidSleeve(r, th, p.z, inner, 20.0, 0.6 * rad, 0.45);
    t.x *= slope;
    s = loomNearer(s, t);
    return vec4(s.x, s.y, 8.0, s.z);
}

vec4 loomMap(vec3 p) {
    float rad = mix(0.07, 0.19, gLoomForm);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * braidSleeves(p, rad);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * braidCables(p, rad);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * braidStar(p, rad);
    return m;
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 1.0);
}
