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
//#include lib_hollow

// Hollow Neuron: a flight through open dark strung with nerve: cell bodies at
// the nodes of a lattice, fibres running between them on every side, the way
// ahead passing between the rows.
//
// Morph feeds the net: the fibres thicken and the cell bodies swell toward
// each other. Worst case 72 steps, no noise.

vec2 hollowMap(vec3 p) {
    vec3 q = p + vec3(1.0, 1.0, 0.0);
    q += 0.12 * sin(q.yzx * 1.5707963);
    vec3 c = q - 2.0 * floor(q * 0.5 + 0.5);
    float thick = 0.035 + 0.1 * gHollowForm;
    float fibre = min(length(c.xy), min(length(c.yz), length(c.zx))) - thick;
    float body = length(c) - (0.3 + 0.4 * gHollowForm);
    float d = hollowSmin(fibre, body, 0.35);
    return vec2(d * 0.85, 4.0 * length(c) + 0.5);
}

//#include lib_hollow_march

void main() {
    vec2 uv = view();
    hollowSetup();
    gHollowStride = 0.8;
    gHollowBore = 0.2;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = hollowFinish(hollowTrace(hollowEye(ro), rd), 2.0);
}
