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

// Flame Veil: the family's opener. Nested silk sheets on a triply periodic
// surface, flown through; every sheet is a level of one field, so they nest
// like the layers of a flame, and a finer second set drifts through them in
// patches.
//
// Morph slides the surface from a gyroid's saddles to a lattice of rounded
// cells, and the sheets with it. Worst case 72 samples, no folds, no noise.

void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt) {
    vec3 q = p * 1.5707963;
    float gy = dot(sin(q), cos(q.yzx));
    float cell = 0.8 * (cos(q.x) + cos(q.y) + cos(q.z));
    float s = mix(gy, cell, gFlameForm);
    f = vec2(1.5 * s + 0.4 * sin(q.z * 0.5 + q.x), 1.1 * dot(sin(q.zxy * 2.0), cos(q * 2.0 + s)));
    hue = vec2(0.1 * sin(q.z * 0.25) + 0.15 * s, 0.5 + 0.2 * s);
    wt = vec2(0.62 + 0.38 * sin(q.x * 0.5 + q.y * 0.75 + 1.7 * s), 0.3 + 0.5 * sin(q.y * 0.5 - q.z * 0.25 + 1.0));
}

//#include lib_flame_march

void main() {
    vec2 uv = view();
    flameSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, FLAME_PERIOD);
    fragColor = flameFinish(flameMarch(ro, rd), 1.0);
}
