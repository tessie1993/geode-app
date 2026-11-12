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

// Loom Curtain: a flight down an aisle between hanging curtains of thread,
// row behind row on either side. Each thread hangs from a rail, sways with
// its row and ends at its own length in a bead; behind every row hangs a
// sheer of gauze the light comes through.
//
// Morph gathers each row's threads into bunches with gaps between them.
// Worst case 90 steps, no noise.

vec4 loomMap(vec3 p) {
    float ax = abs(p.x);
    float k = max(floor((ax - 0.9) / 1.4 + 0.5), 0.0);
    float x = ax - 0.9 - 1.4 * k;
    float row = k + (p.x < 0.0 ? 7.0 : 0.0);
    // Each row sways, and Morph gathers its threads: crowded where `dense` is high, sparse where it is low.
    float z = p.z + 0.12 * sin(0.9 * p.y + uTime * LOOM_RATE(170) + 1.9 * row);
    float gather = 0.22 * gLoomForm;
    float zz = z - gather * sin(3.9269908 * z);
    float dense = max(1.0 - gather * 3.9269908 * cos(3.9269908 * z), 0.1);
    float w = 0.16;
    float n = floor(zz / w + 0.5);
    float across = (zz - w * n) / dense;
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

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 1.0);
}
