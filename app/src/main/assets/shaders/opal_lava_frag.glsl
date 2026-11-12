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
//#include lib_opal

// Opal Lava: six drops of slow liquid drifting round each other, joining where
// they touch and pulling apart again. Turned about.
//
// Morph gathers them: from separate small drops to one heavy mass. Worst case
// 80 steps and 4 skins, no noise.

vec2 opalMap(vec3 p) {
    float d = 1.0e3;
    float join = 0.25 + 0.9 * gOpalForm;
    for (int i = 0; i < 6; i++) {
        float f = float(i);
        vec3 c = (1.75 - 0.9 * gOpalForm) * vec3(
            sin(uTime * OPAL_RATE(300 + 40 * i) + 2.1 * f),
            sin(uTime * OPAL_RATE(220 + 30 * i) + 4.3 * f),
            sin(uTime * OPAL_RATE(260 - 20 * i) + 1.3 * f)
        );
        d = opalSmin(d, length(p - c) - (0.5 + 0.07 * f + 0.25 * gOpalForm), join);
    }
    return vec2(d, 0.5);
}

void main() {
    vec2 uv = view();
    opalSetup();
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.4, CAM_SPIN_UNIT, ro, rd);
    fragColor = opalFinish(opalTrace(ro, rd), 1.1);
}
