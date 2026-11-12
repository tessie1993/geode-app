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
//#include lib_flame

// Flame Aurora: a flight between hanging curtains of light. Upright sheets
// folded along the way, each with a ragged hem, and combed into fine upright
// fibres where planes across the way cut them.
//
// Morph deepens the folds from flat drapes to tight pleats. Worst case 90
// samples, no folds, no noise.

void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt) {
    float zc = 0.3926991 * p.z;
    float pleat = 0.35 + 1.1 * gFlameForm;
    f.x = 0.75 * p.x + 0.9 * sin(2.0 * zc + 0.5 * p.y) + pleat * 0.5 * sin(5.0 * zc + 1.3 * p.x + 0.4 * p.y);
    f.y = 3.0 * p.z + 1.5 * sin(1.1 * p.x + 0.3 * p.y);
    hue = vec2(0.14 * p.y + 0.1 * p.x, 0.5);
    float hem = -1.3 + 0.7 * sin(1.7 * p.x + 3.0 * zc) + 0.3 * sin(4.0 * zc);
    wt = vec2(smoothstep(hem, hem + 2.2, p.y) * smoothstep(5.0, 2.0, p.y), 0.0);
}

//#include lib_flame_march

void main() {
    vec2 uv = view();
    flameSetup();
    gFlameThread = 0.55;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, FLAME_PERIOD);
    fragColor = flameFinish(flameMarch(ro, rd), 1.8);
}
