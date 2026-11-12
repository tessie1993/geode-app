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
//#include lib_hollow

// Hollow Cave: a flight through a cave system. One passage runs ahead, and
// side chambers open off it wherever the rock gives way; the walls are cut
// in level strata, each one a glowing line.
//
// Morph moves the rock: chambers open, join and close again. Worst case 90
// steps, no noise.

vec2 hollowMap(vec3 p) {
    float r = length(p.xy);
    vec3 q = p * 0.7853982;
    float rock = dot(sin(q), cos(q.yzx)) + mix(-0.5, 0.7, gHollowForm) + 0.35 * sin(q.z * 0.5 + 2.0 * q.y);
    float chamber = rock / 1.25;
    float way = 0.85 + 0.2 * sin(q.z + 1.0) - r;
    float d = -hollowSmin(-chamber, -way, 0.5);
    return vec2(d, 4.5 * p.y + 0.8 * sin(q.x * 2.0 + q.z) + 0.5 * rock);
}

//#include lib_hollow_march

void main() {
    vec2 uv = view();
    hollowSetup();
    gHollowStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = hollowFinish(hollowTrace(hollowEye(ro), rd), 1.0);
}
