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

// Loom Cloth: a flight between sheets of open cloth stacked above and below
// the way, each a plain weave: warp threads along the way, weft across it,
// each thread over one and under the next, warp and weft in their own
// colours, every sheet set a little aside from the last.
//
// Morph lifts the sheets into slow billows that run along the way. Worst
// case 90 steps, no noise.

vec4 loomMap(vec3 p) {
    float gap = 2.0;
    float k = floor((p.y - 1.0) / gap + 0.5);
    float y = p.y - 1.0 - gap * k;
    float x = p.x + 0.2 * k;
    y -= 0.25 * gLoomForm * sin(0.6 * x + 0.7853982 * p.z + 1.7 * k);
    float w = 0.4;
    float rad = 0.075;
    float lift = 0.07;
    float i = floor(x / w + 0.5);
    float j = floor(p.z / w + 0.5);
    float si = 1.0 - 2.0 * mod(i, 2.0);
    float sj = 1.0 - 2.0 * mod(j, 2.0);
    // Where warp i meets weft j, cos(PI x / w) is si and cos(PI z / w) is sj: the one is up where the other is down.
    vec3 warp = loomYarn(vec2(x - w * i, y - lift * si * cos(LOOM_PI * p.z / w)), rad, 3.0, 2.0 * p.z, 0.13 * k);
    vec3 weft = loomYarn(vec2(p.z - w * j, y + lift * sj * cos(LOOM_PI * x / w)), rad, 3.0, 2.0 * x, 0.4 + 0.13 * k);
    vec3 s = loomNearer(warp, weft);
    return vec4(s.x, s.y, 1.0e3, s.z);
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    gLoomStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 0.9);
}
