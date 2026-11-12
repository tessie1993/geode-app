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

// Flame Orb: a glass ball with a flame inside it, turned about. The ball is a
// thin shell, bright at its rim, with fainter shells round it; inside, space
// is folded and inverted three times and the veils are the levels of one
// coordinate of the folded space, so they curl into each other.
//
// Morph moves the folds apart, so the flame inside reshapes. Worst case 90
// samples and 3 fold iterations, no noise.

#define ORB_R 1.55

void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt) {
    float l = length(p);
    vec3 q = p;
    q.xz = flameRot(uTime * FLAME_RATE(90)) * q.xz;
    vec3 off = vec3(0.5, 0.36, 0.44) * (0.7 + 0.7 * gFlameForm);
    for (int i = 0; i < 3; i++) {
        q = abs(q) - off;
        q *= clamp(0.62 / dot(q, q), 1.0, 5.0);
        q.xy = flameRot(0.6) * q.xy;
    }
    f = vec2(1.3 * q.y + 0.4 * q.x, 2.2 * (l - ORB_R) + 0.5);
    hue = vec2(0.25 * length(q.xz), 0.55 + 0.2 * p.y);
    float inside = 1.0 - smoothstep(ORB_R - 0.12, ORB_R, l);
    wt = vec2(inside * (0.7 + 0.3 * sin(2.0 * q.z)), l < ORB_R + 0.2 ? 0.8 * step(ORB_R - 0.2, l) : 0.42 * exp(-0.7 * (l - ORB_R)));
}

//#include lib_flame_march

void main() {
    vec2 uv = view();
    flameSetup();
    gFlameGraze = 2.5;
    gFlameFar = 8.0 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.3, CAM_SPIN_UNIT, ro, rd);
    fragColor = flameFinish(flameMarch(ro, rd), 2.2);
}
