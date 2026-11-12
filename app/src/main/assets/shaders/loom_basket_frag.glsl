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

// Loom Basket: a woven ball, turned about. Flat cane strips run pole to pole
// and round it in rings, each strip over the next it crosses and under the
// one after; inside hangs a lantern, one soft skin that breathes, seen
// through the gaps of the weave and lighting the far side from within.
//
// Morph opens the weave: the strips narrow from a closed basket to a sparse
// cage. Worst case 90 steps, no noise.

vec4 loomMap(vec3 p) {
    p.xz = loomRot(uTime * LOOM_RATE(70)) * p.xz;
    float r = length(p);
    float rho = length(p.xz);
    float lon = loomAngle(p.xz);
    float lat = loomAngle(vec2(rho, p.y));
    float big = 1.9;
    float lift = 0.05;
    float wide = mix(0.27, 0.07, gLoomForm);
    float stepLon = LOOM_TAU / 16.0;
    float stepLat = LOOM_PI / 10.0;
    float i = floor(lon / stepLon + 0.5);
    float j = clamp(floor(lat / stepLat + 0.5), -4.0, 4.0);
    float si = 1.0 - 2.0 * mod(i, 2.0);
    float sj = 1.0 - 2.0 * mod(j, 2.0);
    // Where strip i meets ring j, cos(10 lat) is sj and cos(8 lon) is si: the one is out where the other is in.
    vec3 a = loomTape(vec2(rho * (lon - stepLon * i), r - big - lift * si * cos(10.0 * lat)), wide, 4.0, 0.0);
    vec3 b = loomTape(vec2(r * (lat - stepLat * j), r - big + lift * sj * cos(8.0 * lon)), wide, 4.0, 0.45);
    vec3 s = loomNearer(a, b);
    float lantern = r - 1.2 - 0.06 * sin(2.6 * p.x + 1.7 * p.y + uTime * LOOM_RATE(150)) * cos(2.2 * p.z - uTime * LOOM_RATE(110));
    return vec4(s.x, s.y, lantern, s.z);
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.6, CAM_SPIN_UNIT, ro, rd);
    fragColor = loomFinish(loomTrace(ro, rd), 1.1);
}
