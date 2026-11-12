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

// Flame Helix: a flight down the bore of a braid. Nested tubes about the way
// ahead, each wound with four threads, every tube's threads set a little
// further round than the last: only the threads are lit.
//
// Morph shears the winding outward, so the threads lean from a plain braid
// into a whirlpool. Worst case 72 samples, no folds, no noise.

void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt) {
    float r = length(p.xy);
    float th = atan(p.y, p.x);
    f.x = 1.25 * r + 0.35 * sin(2.0 * th - 0.7854 * p.z) + 0.3 * sin(0.3927 * p.z);
    f.y = 4.0 * th / FLAME_TAU + 0.25 * p.z + mix(0.3, 2.4, gFlameForm) * r + uTime * FLAME_RATE(80);
    hue = vec2(0.16 * r + 0.04 * p.z, 0.5 + 0.2 * r);
    wt = vec2(0.85, 0.5) * smoothstep(0.1, 0.35, r);
}

void main() {
    vec2 uv = view();
    flameSetup();
    gFlameThread = 1.0;
    gFlamePeriod.y = 4.0;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, FLAME_PERIOD);
    fragColor = flameFinish(flameMarch(ro, rd), 0.45);
}
