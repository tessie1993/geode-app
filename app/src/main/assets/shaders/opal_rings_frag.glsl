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

// Opal Rings: three glass hoops set through each other like a gyroscope's,
// each ruled round its tube. Turned about.
//
// Morph thickens the rings and tips them, from thin hoops to fat links that
// press together. Worst case 80 steps and 4 skins, no noise.

float ring(vec3 p, float big, float small, out float skin) {
    vec2 t = vec2(length(p.xy) - big, p.z);
    skin = 6.0 * atan(p.y, p.x) / OPAL_TAU + atan(t.y, t.x) / OPAL_TAU;
    return length(t) - small;
}

vec2 opalMap(vec3 p) {
    p.xz = opalRot(uTime * OPAL_RATE(80)) * p.xz;
    float small = mix(0.12, 0.5, gOpalForm);
    float tip = 0.5 * gOpalForm;
    float s0;
    float s1;
    float s2;
    vec3 a = p;
    a.yz = opalRot(tip) * a.yz;
    float d0 = ring(a, 1.7, small, s0);
    vec3 b = p.yzx;
    b.yz = opalRot(tip) * b.yz;
    float d1 = ring(b, 1.7, small, s1);
    vec3 c = p.zxy;
    c.yz = opalRot(tip) * c.yz;
    float d2 = ring(c, 1.7, small, s2);
    float d = opalSmin(d0, opalSmin(d1, d2, 0.15), 0.15);
    return vec2(d, d0 < min(d1, d2) ? s0 : (d1 < d2 ? s1 : s2));
}

void main() {
    vec2 uv = view();
    opalSetup();
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.6, CAM_SPIN_UNIT, ro, rd);
    fragColor = opalFinish(opalTrace(ro, rd), 1.2);
}
