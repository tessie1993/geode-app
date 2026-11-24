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

// Hollow Shell: a flight up the middle of a great spiral shell. One thin wall
// winds outward from the way ahead, turn inside turn, and turns as it goes, so
// the chambers sweep round the eye.
//
// Morph winds the wall from two loose turns to many tight ones. Worst case 90
// steps, no noise.

vec2 hollowMap(vec3 p) {
    float r = max(length(p.xy), 1.0e-3);
    float th = atan(p.y, p.x) / HOLLOW_TAU;
    float b = mix(0.9, 0.42, gHollowForm);
    float turn = log(r) / b - th - 0.125 * p.z;
    float c = turn - floor(turn + 0.5);
    float wall = abs(c) * b * r / sqrt(1.0 + 0.0253 * b * b + 0.62 * b * b * r * r) - 0.035 * (1.0 + r);
    float d = max(wall, 0.55 - r);
    d = min(d, 3.6 - r);
    return vec2(d, 10.0 * (th + 0.125 * p.z) + 6.0 * log(r));
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
