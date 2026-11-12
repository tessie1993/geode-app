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

// Loom Sail: a flight through a tent of taut lines. Rings of seven knots
// drift round the way, every knot tied to its neighbours in its ring and to
// the two nearest in the next, so the lines lace into triangles; some of the
// triangles hold a stretched sail of skin.
//
// Morph rigs the sails: each one grows from its middle until it fills its
// triangle. Worst case 90 steps, no noise.

/** Knot i of ring k, drifting about its place; eight rings to LOOM_PERIOD. */
vec3 sailKnot(float k, float i) {
    float ii = mod(i, 7.0);
    float h = loomHash(vec2(ii, mod(k, 8.0)));
    float a = (ii + 0.25 * sin(uTime * LOOM_RATE(110) + LOOM_TAU * h)) * (LOOM_TAU / 7.0);
    float rr = 1.55 + 0.35 * sin(uTime * LOOM_RATE(80) + 5.0 * h + ii);
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

vec4 loomMap(vec3 p) {
    float k = floor(p.z * 0.5);
    float i = floor(loomAngle(p.xy) / (LOOM_TAU / 7.0));
    // The four knots round this cell of the tent: a, b on ring k, c, d on ring k + 1.
    vec3 a = sailKnot(k, i);
    vec3 b = sailKnot(k, i + 1.0);
    vec3 c = sailKnot(k + 1.0, i);
    vec3 d = sailKnot(k + 1.0, i + 1.0);
    float rad = 0.03;
    vec3 s = loomStrand(p, a, c, rad, 0.0);
    s = loomNearer(s, loomStrand(p, b, d, rad, 0.0));
    s = loomNearer(s, loomStrand(p, a, d, rad, 0.3));
    s = loomNearer(s, loomStrand(p, a, b, rad, 0.6));
    s = loomNearer(s, loomStrand(p, c, d, rad, 0.6));
    float knot = min(min(length(p - a), length(p - b)), min(length(p - c), length(p - d))) - 0.09;
    s = loomNearer(s, vec3(knot, 0.0, 0.85));
    // One of the cell's two triangles may hold a sail, grown from its middle by Morph.
    float h = loomHash(vec2(mod(i, 7.0), mod(k, 8.0) + 11.0));
    float grow = clamp(2.0 * gLoomForm - h, 0.0, 1.0);
    float sail = 1.0e3;
    if (grow > 0.05) {
        vec3 v = h > 0.5 ? b : c;
        vec3 mid = (a + v + d) / 3.0;
        sail = sailTriangle(p, mid + (a - mid) * grow, mid + (v - mid) * grow, mid + (d - mid) * grow);
    }
    return vec4(s.x, s.y, sail, s.z);
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
