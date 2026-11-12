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

// Opal Drop: one heavy drop the moment it breaks: a body with a crown of eight
// droplets thrown out round it, each still on its neck. Turned about.
//
// Morph throws the crown: from buds on the body, out on thinning necks, to
// free droplets. Worst case 100 steps and 4 skins, no noise.

vec2 opalMap(vec3 p) {
    p.xz = opalRot(uTime * OPAL_RATE(60)) * p.xz;
    float body = length(p * vec3(1.0, 1.0 + 0.15 * sin(uTime * OPAL_RATE(700)), 1.0)) - 1.15;
    float seg = OPAL_TAU / 8.0;
    float a = mod(atan(p.z, p.x) + 0.5 * seg, seg) - 0.5 * seg;
    float r = length(p.xz);
    vec3 q = vec3(r * cos(a), p.y, r * sin(a));
    float out_ = mix(1.15, 2.6, gOpalForm);
    vec3 tip = vec3(out_, 0.35 * gOpalForm + 0.25, 0.0);
    float bud = length(q - tip) - 0.3;
    vec3 ab = tip - vec3(0.9, 0.0, 0.0);
    float along = clamp(dot(q - vec3(0.9, 0.0, 0.0), ab) / dot(ab, ab), 0.0, 1.0);
    float neck = length(q - vec3(0.9, 0.0, 0.0) - ab * along) - 0.16 * (1.0 - 1.2 * gOpalForm);
    float d = opalSmin(body, opalSmin(bud, neck, 0.2), 0.3);
    return vec2(d, 5.0 * length(p) + 0.5);
}

//#include lib_opal_march

void main() {
    vec2 uv = view();
    opalSetup();
    gOpalLines *= 0.4;
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.4, CAM_SPIN_UNIT, ro, rd);
    fragColor = opalFinish(opalTrace(ro, rd), 1.1);
}
