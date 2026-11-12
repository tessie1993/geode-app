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

// Flame Knot: threads wound round nested rings, three poloidal turns for
// every two the long way, and the whole folded into itself three times, so
// the windings read as one tangled knot. Turned about.
//
// Morph turns the folds, and the tangle reties itself. Worst case 72 samples
// and 3 fold iterations, no noise.

void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt) {
    vec3 q = p;
    q.xz = flameRot(uTime * FLAME_RATE(60)) * q.xz;
    float a = 0.35 + 0.7 * gFlameForm;
    for (int i = 0; i < 3; i++) {
        q = abs(q) - vec3(0.55, 0.3, 0.42);
        q.xy = flameRot(a) * q.xy;
        q.yz = flameRot(0.5 * a + 0.4) * q.yz;
    }
    vec2 tor = vec2(length(q.xy) - 0.95, q.z);
    float lt = length(tor);
    f = vec2(3.2 * lt, (3.0 * atan(tor.y, tor.x) - 2.0 * atan(q.y, q.x)) / FLAME_TAU);
    hue = vec2(0.5 * lt + 0.1 * q.x, 0.5 + 0.3 * lt);
    float reach = smoothstep(0.75, 0.4, lt) * smoothstep(3.6, 2.6, length(p));
    wt = reach * vec2(0.9, 0.6);
}

void main() {
    vec2 uv = view();
    flameSetup();
    gFlameThread = 0.85;
    gFlamePeriod.y = 1.0;
    gFlameFar = 8.0 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.6, CAM_SPIN_UNIT, ro, rd);
    fragColor = flameFinish(flameMarch(ro, rd), 1.5);
}
