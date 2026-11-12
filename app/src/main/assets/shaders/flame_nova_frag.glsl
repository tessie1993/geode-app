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

// Flame Nova: nested lobes about one axis, the shape of a field round a
// magnet, with eight meridian sheets that spiral out through them; turned
// about. The lobes are whole veils, brightest where a meridian meets them.
//
// Morph pinches the lobes from near-spheres to deep-waisted rings. Worst case
// 72 samples, no folds, no noise.

void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt) {
    float l = max(length(p), 1.0e-3);
    float ll = log(l);
    float waist = max(1.0 - p.y * p.y / (l * l), 1.0e-3);
    float th = atan(p.z, p.x);
    f.x = 2.0 * (ll - mix(0.35, 1.5, gFlameForm) * log(waist)) - uTime * FLAME_RATE(45);
    f.y = 8.0 * th / FLAME_TAU + 1.4 * ll;
    hue = vec2(0.3 * ll - 0.1 * log(waist), 0.5 + 0.2 * ll);
    float reach = smoothstep(3.6, 2.2, l) * smoothstep(0.0, 0.08, waist);
    wt = reach * vec2(0.85, 0.35);
}

void main() {
    vec2 uv = view();
    flameSetup();
    gFlameThread = 0.45;
    gFlamePeriod.y = 8.0;
    gFlameFar = 8.0 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.5, CAM_SPIN_UNIT, ro, rd);
    fragColor = flameFinish(flameMarch(ro, rd), 1.1);
}
