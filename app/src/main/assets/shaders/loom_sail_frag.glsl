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

// Loom Sail: a flight through a tent of taut lines. Rings of knots drift round
// the way, every knot tied to its neighbours in its ring and to the two
// nearest in the next, so the lines lace into triangles; some of the
// triangles hold a stretched sail of skin.
//
// Three formulas (Formula, lib_layers), each a different rig:
//   0  rings of seven knots, a ring every 2 units
//   1  rings of five on a rig wound once round the way in every period, so
//      the lines lace into a spiral of long triangles
//   2  rings of ten whose knots stand alternately in and out, so the tent's
//      cross-section is a star and its sails slant across its points
//
// Morph rigs the sails: each one grows from its middle until it fills its
// triangle. Worst case 90 steps, no noise.

/** Knot i of ring k of a rig with n knots a ring, every other ring turned by `stagger` cells, knots alternately `star` in and out; eight rings to LOOM_PERIOD. */
vec3 sailKnot(float k, float i, float n, float stagger, float star) {
    float kk = mod(k, 8.0);
    float ii = mod(i, n);
    float h = loomHash(vec2(ii, kk));
    float a = (ii + stagger * mod(k, 2.0) + 0.25 * sin(uTime * LOOM_RATE(110) + LOOM_TAU * h)) * (LOOM_TAU / n);
    float rr = 1.55 + 0.35 * sin(uTime * LOOM_RATE(80) + 5.0 * h + ii) + star * (1.0 - 2.0 * mod(ii, 2.0));
    return vec3(rr * cos(a), rr * sin(a), 2.0 * k + 0.4 * sin(uTime * LOOM_RATE(130) + 9.0 * h));
}

/** The distance from p to the triangle abc, a flat skin with no thickness. */
float sailTriangle(vec3 p, vec3 a, vec3 b, vec3 c) {
    vec3 ba = b - a;
    vec3 pa = p - a;
    vec3 cb = c - b;
    vec3 pb = p - b;
    vec3 ac = a - c;
    vec3 pc = p - c;
    vec3 nor = cross(ba, ac);
    float over = sign(dot(cross(ba, nor), pa)) + sign(dot(cross(cb, nor), pb)) + sign(dot(cross(ac, nor), pc));
    if (over < 2.0) {
        vec3 e0 = ba * clamp(dot(ba, pa) / max(dot(ba, ba), 1.0e-6), 0.0, 1.0) - pa;
        vec3 e1 = cb * clamp(dot(cb, pb) / max(dot(cb, cb), 1.0e-6), 0.0, 1.0) - pb;
        vec3 e2 = ac * clamp(dot(ac, pc) / max(dot(ac, ac), 1.0e-6), 0.0, 1.0) - pc;
        return sqrt(min(min(dot(e0, e0), dot(e1, e1)), dot(e2, e2)));
    }
    return abs(dot(nor, pa)) / max(length(nor), 1.0e-6);
}

/** The cell of the rig round p: its lines, knots and (grown by Morph) one sail. */
vec4 sailRig(vec3 p, float n, float stagger, float star, float hue) {
    float k = floor(p.z * 0.5);
    float cell = LOOM_TAU / n;
    float i = floor(loomAngle(p.xy) / cell - stagger * mod(k, 2.0));
    float j = floor(loomAngle(p.xy) / cell - stagger * mod(k + 1.0, 2.0));
    // The four knots round this cell of the tent: a, b on ring k, c, d on ring k + 1.
    vec3 a = sailKnot(k, i, n, stagger, star);
    vec3 b = sailKnot(k, i + 1.0, n, stagger, star);
    vec3 c = sailKnot(k + 1.0, j, n, stagger, star);
    vec3 d = sailKnot(k + 1.0, j + 1.0, n, stagger, star);
    float rad = 0.03;
    vec3 s = loomStrand(p, a, c, rad, hue);
    s = loomNearer(s, loomStrand(p, b, d, rad, hue));
    s = loomNearer(s, loomStrand(p, a, d, rad, hue + 0.3));
    s = loomNearer(s, loomStrand(p, a, b, rad, hue + 0.6));
    s = loomNearer(s, loomStrand(p, c, d, rad, hue + 0.6));
    float knot = min(min(length(p - a), length(p - b)), min(length(p - c), length(p - d))) - 0.09;
    s = loomNearer(s, vec3(knot, 0.0, hue + 0.85));
    // One of the cell's two triangles may hold a sail, grown from its middle by Morph.
    float h = loomHash(vec2(mod(i, n), mod(k, 8.0) + 11.0));
    float grow = clamp(2.0 * gLoomForm - h, 0.0, 1.0);
    float sail = 8.0;
    if (grow > 0.05) {
        vec3 v = h > 0.5 ? b : c;
        vec3 mid = (a + v + d) / 3.0;
        sail = sailTriangle(p, mid + (a - mid) * grow, mid + (v - mid) * grow, mid + (d - mid) * grow);
    }
    return vec4(s.x, s.y, sail, s.z);
}

vec4 loomMap(vec3 p) {
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * sailRig(p, 7.0, 0.0, 0.0, 0.0);
    if (gLayerPick.y > 0.0) {
        // Wound once round the way in a period, so the world still repeats; the twist's shear bounds the step.
        vec3 q = vec3(loomRot(p.z * (LOOM_TAU / LOOM_PERIOD)) * p.xy, p.z);
        vec4 w = sailRig(q, 5.0, 0.0, 0.0, 0.2);
        m += gLayerPick.y * vec4(0.75 * w.x, w.y, 0.75 * w.z, w.w);
    }
    if (gLayerPick.z > 0.0) m += gLayerPick.z * sailRig(p, 10.0, 0.0, 0.55, 0.45);
    return m;
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    gLoomStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 1.0);
}
