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

// Hollow Hive: a flight through a string of round chambers, each joined to
// the next by a narrow gate, their walls ringed in level lines like a wasp
// nest's paper.
//
// Morph opens the gates until the chambers run together into one pipe. Worst
// case 72 steps, no noise.

vec2 hollowMap(vec3 p) {
    float r = length(p.xy);
    float z = p.z - 4.0 * floor(p.z * 0.25 + 0.5);
    float room = 1.75 - length(vec3(p.xy, z * 0.95));
    float gate = mix(0.5, 1.7, gHollowForm) + 0.1 * sin(5.0 * atan(p.y, p.x)) - r;
    float d = -hollowSmin(-room, -gate, 0.45);
    return vec2(d, 5.0 * length(vec3(p.xy * 0.6, z)) + 0.5);
}

void main() {
    vec2 uv = view();
    hollowSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = hollowFinish(hollowTrace(hollowEye(ro), rd), 1.0);
}
