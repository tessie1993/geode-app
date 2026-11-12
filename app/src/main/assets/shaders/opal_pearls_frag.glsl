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

// Opal Pearls: a flight down five strings of pearls wound round the way
// ahead, each pearl a small bubble on a thread.
//
// Morph grows the pearls until each string runs together into one beaded
// rope. Worst case 100 steps and 4 skins, no noise.

vec2 opalMap(vec3 p) {
    p.xy = opalRot(0.3926991 * p.z) * p.xy;
    float seg = OPAL_TAU / 5.0;
    float a = mod(atan(p.y, p.x) + 0.5 * seg, seg) - 0.5 * seg;
    float r = length(p.xy);
    vec2 q = vec2(r * cos(a) - 1.05, r * sin(a));
    float gap = 0.5;
    float z = p.z - gap * floor(p.z / gap + 0.5);
    float size = mix(0.13, 0.36, gOpalForm);
    float bead = length(vec3(q, z)) - size;
    float thread = length(q) - mix(0.02, 0.24, gOpalForm);
    return vec2(opalSmin(bead, thread, 0.08 + 0.2 * gOpalForm), 0.5);
}

//#include lib_opal_march

void main() {
    vec2 uv = view();
    opalSetup();
    gOpalStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, OPAL_PERIOD);
    fragColor = opalFinish(opalTrace(ro, rd), 1.3);
}
