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

// Opal Nautilus: one shell wound on a logarithmic spiral, each turn wider than
// the last by the same share, with the chamber walls drawn as fine lines round
// the tube. Turned about.
//
// Morph winds the spiral from a loose horn to a tight coil. Worst case 80
// steps and 4 skins, no noise.

vec2 opalMap(vec3 p) {
    p.xz = opalRot(uTime * OPAL_RATE(70)) * p.xz;
    float r = max(length(p.xy), 1.0e-3);
    float th = atan(p.y, p.x) / OPAL_TAU;
    float b = mix(1.15, 0.5, gOpalForm);
    float turn = log(r) / b - th;
    float k = floor(turn + 0.5);
    float d = 1.0e3;
    float coil = 0.0;
    for (int i = -1; i <= 0; i++) {
        float kk = k + float(i) + 0.35;
        float rk = exp(b * (th + kk));
        float di = length(vec2(r - rk, p.z)) - 0.4 * rk * (1.0 - exp(-b));
        if (di < d) {
            d = di;
            coil = kk + th;
        }
    }
    d = max(d, length(p) - 3.0);
    return vec2(d, 14.0 * coil);
}

//#include lib_opal_march

void main() {
    vec2 uv = view();
    opalSetup();
    gOpalStride = 0.6;
    vec3 ro;
    vec3 rd;
    camAround(uv, 7.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = opalFinish(opalTrace(ro, rd), 1.1);
}
