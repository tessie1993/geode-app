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

// Flame Lotus: a blossom of nested petal shells, turned about. Each shell is a
// sphere pushed out into five petals that wind a little with every layer;
// meridian sheets spiral out through them.
//
// Morph opens the bud: the petals deepen from a near-sphere to a full bloom.
// Worst case 72 samples, no folds, no noise.

void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt) {
    float l = max(length(p), 1.0e-3);
    vec3 dir = p / l;
    float th = atan(p.z, p.x);
    float ll = log(l);
    float waist = 1.0 - dir.y * dir.y;
    float open = mix(0.12, 1.0, gFlameForm);
    float petal = cos(5.0 * th + 2.0 * ll + uTime * FLAME_RATE(70)) * waist;
    f = vec2(2.6 * ll + open * petal + 0.7 * dir.y, 5.0 * th / FLAME_TAU + 1.1 * ll - 0.6 * dir.y);
    hue = vec2(0.3 * ll + 0.1 * petal, 0.5 + 0.25 * ll);
    float reach = smoothstep(3.4, 2.2, l) * smoothstep(0.0, 0.15, waist);
    wt = reach * vec2(0.75 + 0.25 * petal, 0.5 * waist);
}

//#include lib_flame_march

void main() {
    vec2 uv = view();
    flameSetup();
    gFlamePeriod.y = 5.0;
    gFlameFar = 8.0 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.4, CAM_SPIN_UNIT, ro, rd);
    fragColor = flameFinish(flameMarch(ro, rd), 1.3);
}
