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
//#include lib_fluor

// Fluor Lattice: a flight through pierced screens that stand across the way,
// one every two units. Each is a lattice of bars studded at every crossing,
// round an opening ringed by a heavier moulding, and each is turned from the
// last, so the screens behind show through the holes as a shifting weave.
//
// Morph fills the lattice: the bars and studs thicken and the opening draws
// in. Worst case 90 steps, no noise.

vec2 fluorMap(vec3 p) {
    float k = floor(p.z * 0.5 + 0.5);
    float z = p.z - 2.0 * k;
    float r = length(p.xy);
    vec2 u = fluorRot(0.7853982 + 0.3926991 * mod(k, 8.0)) * p.xy;
    vec2 g = (fract(u * 2.5 + 0.5) - 0.5) * 0.4;
    float mouth = mix(1.05, 0.85, gFluorForm);
    float open = mouth - r;
    float bars = fluorSlab(max(min(abs(g.x), abs(g.y)) - mix(0.025, 0.065, gFluorForm), open), z, 0.04);
    float stud = max(length(vec3(g, z)) - (0.06 + 0.03 * gFluorForm), open);
    float ring = fluorSlab(abs(r - mouth - 0.07) - 0.07, z, 0.09);
    float d = bars;
    float paint = 0.381966 * mod(k, 8.0);
    if (stud < d) {
        d = stud;
        paint = 0.25;
    }
    if (ring < d) {
        d = ring;
        paint = 0.5;
    }
    return vec2(d, paint);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    gFluorStride = 0.8;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.0);
}
