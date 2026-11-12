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

// Flame Spiral: a flight down the eye of a three-armed spiral. The arms are
// sheets that wind about the way ahead and turn with depth; rings of the same
// logarithmic spacing cross them, so the web is the same at every scale.
//
// Morph winds the arms tighter. Worst case 90 samples, no folds, no noise.

void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt) {
    float r = max(length(p.xy), 1.0e-3);
    float th = atan(p.y, p.x);
    float lr = log(r);
    float wind = mix(0.5, 1.7, gFlameForm);
    f.x = 3.0 * th / FLAME_TAU + wind * lr + 0.375 * p.z + uTime * FLAME_RATE(60);
    f.y = 1.7 * lr + 0.55 * sin(0.7854 * p.z + th) + 0.2 * sin(3.0 * th + 0.3927 * p.z);
    hue = vec2(0.3 * lr, 0.5 + 0.2 * lr);
    float core = smoothstep(0.03, 0.3, r);
    wt = core * vec2(0.65 + 0.35 * sin(2.0 * lr + th), 0.5 + 0.4 * sin(th * 2.0 + 0.3927 * p.z));
}

//#include lib_flame_march

void main() {
    vec2 uv = view();
    flameSetup();
    gFlamePeriod.x = 3.0;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, FLAME_PERIOD);
    fragColor = flameFinish(flameMarch(ro, rd), 0.6);
}
