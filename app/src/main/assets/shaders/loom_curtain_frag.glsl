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

// Loom Curtain: a flight down an aisle between curtains of thread, row behind
// row, each behind a sheer of gauze the light comes through.
//
// Three formulas (Formula, lib_layers), each a different aisle:
//   0  rows hanging on either side: each thread hangs from a rail, sways with
//      its row and ends at its own length in a bead
//   1  a hexagonal harp: six walls of taut strings round the way, row behind
//      row, a bead sliding along every string
//   2  a twisted aisle: the hanging rows wound about the way once a period, so
//      the walls turn into a helix overhead and back
//
// Morph gathers each row's threads into bunches with gaps between them.
// Worst case 90 steps, no noise.

/** Morph's gathering along a row: the thread's place, and how crowded the threads are there. */
vec2 curtainGather(float z) {
    float gather = 0.22 * gLoomForm;
    return vec2(z - gather * sin(3.9269908 * z), max(1.0 - gather * 3.9269908 * cos(3.9269908 * z), 0.1));
}

vec4 curtainRows(vec3 p) {
    float ax = abs(p.x);
    float k = max(floor((ax - 0.9) / 1.4 + 0.5), 0.0);
    float x = ax - 0.9 - 1.4 * k;
    float row = k + (p.x < 0.0 ? 7.0 : 0.0);
    // Each row sways, and Morph gathers its threads: crowded where `dense` is high, sparse where it is low.
    float z = p.z + 0.12 * sin(0.9 * p.y + uTime * LOOM_RATE(170) + 1.9 * row);
    vec2 g = curtainGather(z);
    float w = 0.16;
    float n = floor(g.x / w + 0.5);
    float across = (g.x - w * n) / g.y;
    // A hundred threads to a row in LOOM_PERIOD, each its own length.
    float h = loomHash(vec2(mod(n, 100.0), row));
    float end = -1.1 - 0.9 * h;
    float rad = 0.03;
    float hue = 0.11 * row + 0.3 * h;
    float hang = p.y - clamp(p.y, end, 2.6);
    vec3 s = vec3(length(vec3(x, across, hang)) - rad, 2.0 * loomAngle(vec2(x, across)) / LOOM_TAU + 3.0 * p.y, hue);
    s = loomNearer(s, vec3(length(vec3(x, across, p.y - end)) - 2.4 * rad, 0.0, hue + 0.2));
    s = loomNearer(s, vec3(length(vec2(x, p.y - 2.6)) - 0.06, 0.5 * p.z, 0.6));
    float gauze = length(vec2(x - 0.3, max(-1.0 - p.y, 0.0)));
    return vec4(s.x * 0.9, s.y, gauze, s.z);
}

vec4 curtainHarp(vec3 p) {
    vec2 q = layerSector(p.xy, 6.0);
    float k = max(floor((q.x - 1.0) / 1.3 + 0.5), 0.0);
    float x = q.x - 1.0 - 1.3 * k;
    vec2 g = curtainGather(p.z);
    float w = 0.16;
    float n = floor(g.x / w + 0.5);
    float across = (g.x - w * n) / g.y;
    float h = loomHash(vec2(mod(n, 100.0), k + 20.0));
    float hue = 0.13 * k + 0.3 * h;
    // The strings run taut across each wall; a bead slides along each within the wall's width.
    vec3 s = vec3(length(vec2(x, across)) - 0.024, 2.0 * loomAngle(vec2(x, across)) / LOOM_TAU + 3.0 * q.y, hue);
    float slide = 0.5 * q.x * sin(uTime * LOOM_RATE(120) + LOOM_TAU * h);
    s = loomNearer(s, vec3(length(vec3(x, across, q.y - slide)) - 0.065, 0.0, hue + 0.25));
    return vec4(s.x * 0.85, s.y, abs(x - 0.35), s.z);
}

vec4 curtainTwist(vec3 p) {
    // Once round the way in a period, so the world still repeats; the twist's shear bounds the step.
    p.xy = loomRot(p.z * (LOOM_TAU / LOOM_PERIOD)) * p.xy;
    vec4 m = curtainRows(p);
    return vec4(m.x * 0.7, m.y, m.z * 0.7, m.w + 0.15);
}

vec4 loomMap(vec3 p) {
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * curtainRows(p);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * curtainHarp(p);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * curtainTwist(p);
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
