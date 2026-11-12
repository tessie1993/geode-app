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

// Hollow Bone: a flight through the spongy inside of a bone: rooms joined on
// all six sides through round windows, the struts between them pitted with a
// finer sponge of their own.
//
// Morph ages the bone: from thick walls with small windows down to thin
// struts. Worst case 72 steps, no noise.

vec2 hollowMap(vec3 p) {
    vec3 q = p * 1.5707963;
    vec3 c = cos(q);
    float f = c.x + c.y + c.z + mix(0.55, -0.75, gHollowForm);
    float room = f / (1.5707963 * max(length(sin(q)), 0.6));
    vec3 q2 = p * 6.2831853;
    float pit = dot(sin(q2), cos(q2.yzx)) * 0.03 + 0.03;
    return vec2(room + pit, 2.5 * (c.x * c.y + c.y * c.z + c.z * c.x) + 0.5);
}

void main() {
    vec2 uv = view();
    hollowSetup();
    gHollowStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = hollowFinish(hollowTrace(hollowEye(ro), rd), 1.5);
}
