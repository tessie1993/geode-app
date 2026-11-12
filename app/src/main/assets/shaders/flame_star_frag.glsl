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

// Flame Star: a flight through nested eight-pointed stars. The plane across
// the way is folded into sixteen mirrored wedges and a straight edge is drawn
// in one, at every scale; planes across the way cut those star tubes, and
// only the cuts are lit, so the stars hang one behind the other as outlines.
//
// Morph sharpens the points from a soft octagon to a spiked star. Worst case
// 72 samples, one fold, no noise.

void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt) {
    float r = max(length(p.xy), 1.0e-3);
    float seg = FLAME_TAU / 8.0;
    float a = abs(mod(atan(p.y, p.x) + 0.0981748 * p.z, seg) - 0.5 * seg);
    float tip = mix(0.15, 1.05, gFlameForm);
    float edge = log(r * cos(a - tip) / cos(tip));
    f = vec2(2.4 * edge - 0.125 * p.z + 0.4 * sin(0.7854 * p.z), 0.5 * p.z + 0.7 * edge + uTime * FLAME_RATE(40));
    hue = vec2(0.22 * edge + 0.03 * p.z, 0.4 + 0.3 * edge);
    float core = smoothstep(0.04, 0.3, r);
    wt = core * vec2(0.8, 0.55 + 0.3 * sin(4.0 * a + p.z));
}

//#include lib_flame_march

void main() {
    vec2 uv = view();
    flameSetup();
    gFlameThread = 0.75;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, FLAME_PERIOD);
    fragColor = flameFinish(flameMarch(ro, rd), 1.0);
}
