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

// Loom Net: a flight down a tunnel of netting, a knot at every corner, and a
// wider net round it seen through the meshes.
//
// Three formulas (Formula, lib_layers), each a different netting:
//   0  hexagonal meshes, two nets one inside the other
//   1  diamond meshes knotted at every crossing, the inner net drawn into a
//      string of waists along the way
//   2  a net of nets: inside every hexagonal mesh a finer hexagonal net three
//      times as dense, grown in by the iteration (Detail, k1 TEXTURE, k4 BUILD)
//
// Morph webs the net: a skin grows in from every thread until the meshes
// close into one translucent tube. Worst case 90 steps, no noise.

/** One hexagonal net about the way, radius `big`: `around` meshes round, `rows` pairs of rows in LOOM_PERIOD. */
vec4 netLayer(float r, float th, float z, float big, float around, float rows, float rad, float hue) {
    // The net unrolled: a mesh one unit across, its pairs of rows sqrt(3) apart.
    vec2 g = vec2(around * th / LOOM_TAU, z * rows * 1.7320508 / LOOM_PERIOD);
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

/** One diamond net about the way: threads along both diagonals of the unrolled mesh, a knot where they cross. */
vec4 netDiamond(float r, float th, float z, float big, float around, float rows, float rad, float hue) {
    vec2 g = vec2(around * th / LOOM_TAU, z * rows / LOOM_PERIOD);
    float u = g.x + g.y;
    float v = g.x - g.y;
    float du = loomRep(u, 1.0);
    float dv = loomRep(v, 1.0);
    // The unrolled mesh's unit, world units, along the circumference.
    float unit = big * LOOM_TAU / around;
    float e = min(abs(du), abs(dv)) * 0.7071068;
    float thread = length(vec2(e * unit, r - big)) - rad;
    float knot = length(vec3(du * unit * 0.7071068, dv * unit * 0.7071068, r - big)) - (1.9 + gLoomForm) * rad;
    float web = mix(-0.1, 0.4, gLoomForm);
    float skin = length(vec2(max(e - web, 0.0) * unit, r - big));
    float fibre = knot < thread ? 0.0 : 2.0 * loomAngle(vec2(e, r - big)) / LOOM_TAU + 2.0 * (u + v);
    return vec4(min(thread, knot), fibre, skin, hue + 0.25 * step(knot, thread));
}

vec4 netNearer(vec4 a, vec4 b) {
    vec4 s = a.x < b.x ? a : b;
    s.z = min(a.z, b.z);
    return s;
}

vec4 netHex(vec3 p, float rad) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    return netNearer(netLayer(r, th, p.z, 1.6, 7.0, 6.0, rad, 0.0), netLayer(r, th, p.z, 2.9, 12.0, 6.0, 1.2 * rad, 0.45));
}

vec4 netWaist(vec3 p, float rad) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    // The inner net drawn in at four waists a period; its slope along the way bounds the step.
    float big = 1.7 - 0.45 * (0.5 + 0.5 * cos(4.0 * p.z * (LOOM_TAU / LOOM_PERIOD)));
    vec4 a = netDiamond(r, th, p.z, big, 10.0, 24.0, rad, 0.1);
    a.x *= 0.85;
    return netNearer(a, netDiamond(r, th, p.z, 3.0, 16.0, 24.0, 1.2 * rad, 0.55));
}

vec4 netNested(vec3 p, float rad) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    vec4 m = netLayer(r, th, p.z, 1.7, 7.0, 6.0, 1.2 * rad, 0.2);
    // The finer net in every mesh, three times as dense, grown in by the iteration.
    float grow = clamp(layerIters(0.2, 1.6), 0.0, 1.0);
    if (grow > 0.0) {
        vec4 f = netLayer(r, th, p.z, 1.7, 21.0, 18.0, 0.6 * rad * grow, 0.65);
        f.x += 0.2 * (1.0 - grow);
        m = netNearer(m, f);
    }
    return netNearer(m, netLayer(r, th, p.z, 3.1, 12.0, 6.0, 1.2 * rad, 0.45));
}

vec4 loomMap(vec3 p) {
    float rad = 0.035 + 0.03 * gLoomForm;
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * netHex(p, rad);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * netWaist(p, rad);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * netNested(p, rad);
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
