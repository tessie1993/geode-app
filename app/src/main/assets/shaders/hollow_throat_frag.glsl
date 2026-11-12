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

// Hollow Throat: a flight down a gullet. Soft walls in long folds, and slow
// rings of muscle that close the passage almost shut and pass on down it.
//
// Morph works the muscle: the rings squeeze tighter and the folds deepen.
// Worst case 90 steps, no noise.

vec2 hollowMap(vec3 p) {
    float r = length(p.xy);
    float th = atan(p.y, p.x);
    float squeeze = 0.5 + 0.5 * sin(0.7853982 * p.z - uTime * HOLLOW_RATE(260));
    squeeze *= squeeze;
    float bore = 1.65 - (0.35 + 0.65 * gHollowForm) * squeeze;
    float fold = sin(9.0 * th + 0.6 * sin(0.7853982 * p.z));
    float d = bore - r - (0.06 + 0.16 * gHollowForm) * (0.4 + squeeze) * fold;
    return vec2(d * 0.8, 9.0 * th / HOLLOW_TAU + 0.3 * sin(1.5707963 * p.z));
}

//#include lib_hollow_march

void main() {
    vec2 uv = view();
    hollowSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = hollowFinish(hollowTrace(hollowEye(ro), rd), 1.0);
}
