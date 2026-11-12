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

// Opal Cells: a flight through one unbroken membrane that divides space into
// rooms and the passages between them, the way ahead running down a passage.
//
// Morph moves the membrane through its lattice: closed rooms open into pipes
// and close again the other way round. Worst case 80 steps and 4 skins, no
// noise.

vec2 opalMap(vec3 p) {
    vec3 q = p * 1.5707963;
    vec3 c = cos(q);
    vec3 s = sin(q);
    float f = c.x + c.y + c.z + mix(-0.95, 0.95, gOpalForm);
    float g = 1.5707963 * length(s);
    return vec2(f / max(g, 0.7), 3.0 * (c.x * c.y + c.z) + 0.5);
}

void main() {
    vec2 uv = view();
    opalSetup();
    gOpalStride = 0.6;
    gOpalLines *= 0.5;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, OPAL_PERIOD);
    fragColor = opalFinish(opalTrace(ro, rd), 0.9);
}
