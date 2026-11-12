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

// Loom Net: a flight down a tunnel of hexagonal netting, a knot at every
// corner, and a wider net round it seen through the meshes.
//
// Morph webs the net: a skin grows in from every thread until the meshes
// close into one translucent tube. Worst case 90 steps, no noise.

/** One net about the way, radius `big`: `around` meshes round, six pairs of rows in LOOM_PERIOD. */
vec4 netLayer(float r, float th, float z, float big, float around, float rad, float hue) {
    // The net unrolled: a mesh one unit across, its pairs of rows sqrt(3) apart.
    vec2 g = vec2(around * th / LOOM_TAU, z * 6.0 * 1.7320508 / LOOM_PERIOD);
    vec2 sq = vec2(1.0, 1.7320508);
    vec2 a = mod(g, sq) - 0.5 * sq;
    vec2 b = mod(g - 0.5 * sq, sq) - 0.5 * sq;
    vec2 h = dot(a, a) < dot(b, b) ? a : b;
    vec2 ah = abs(h);
    // How far inside its mesh, 0 on the thread.
    float e = max(0.5 - max(ah.x, dot(ah, vec2(0.5, 0.8660254))), 0.0);
    float unit = big * LOOM_TAU / around;
    vec2 q = vec2(e * unit, r - big);
    // The knot at the nearest corner.
    float ca = loomAngle(h);
    float corner = 0.5235988 + 1.0471976 * floor((ca - 0.5235988) / 1.0471976 + 0.5);
    vec2 k = h - 0.5773503 * vec2(cos(corner), sin(corner));
    float knot = length(vec2(length(k) * unit, r - big)) - (1.8 + gLoomForm) * rad;
    float thread = length(q) - rad;
    float web = mix(-0.1, 0.6, gLoomForm);
    float skin = length(vec2(max(e - web, 0.0) * unit, r - big));
    float fibre = knot < thread ? 0.0 : 2.0 * loomAngle(q) / LOOM_TAU + 12.0 * ca / LOOM_TAU;
    return vec4(min(thread, knot), fibre, skin, hue + 0.2 * step(knot, thread));
}

vec4 loomMap(vec3 p) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    float rad = 0.035 + 0.03 * gLoomForm;
    vec4 a = netLayer(r, th, p.z, 1.6, 7.0, rad, 0.0);
    vec4 b = netLayer(r, th, p.z, 2.9, 12.0, 1.2 * rad, 0.45);
    vec4 s = a.x < b.x ? a : b;
    s.z = min(a.z, b.z);
    return s;
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
