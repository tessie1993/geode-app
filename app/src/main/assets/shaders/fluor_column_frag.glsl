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

// Fluor Column: a flight down a colonnade in the dark, rows of carved columns
// to either side rising and falling out of sight. Two ropes wind up every
// shaft, against the ones on the next, and banded rings cross them; the ropes
// climb slowly as they turn.
//
// Morph embosses the shafts: the ropes and rings stand further out. Worst
// case 90 steps, no noise.

vec2 fluorMap(vec3 p) {
    float ix = floor((p.x - 1.6) / 3.2 + 0.5);
    float iz = floor(p.z * 0.5);
    vec3 q = vec3(p.x - 1.6 - 3.2 * ix, p.y, p.z - 2.0 * iz - 1.0);
    float h = length(q.xz) - 0.38;
    float side = 2.0 * mod(ix + iz, 2.0) - 1.0;
    float s = 2.0 * side * fluorAngle(q.xz) / FLUOR_TAU + q.y / 0.9 + uTime * FLUOR_RATE(50) / FLUOR_TAU;
    float top = mix(0.04, 0.12, gFluorForm);
    // The rope's spacing across itself, 1 / |grad s| on the shaft.
    float rope = fluorStrip(h, (fract(s) - 0.5) * 0.718, 0.06, top);
    float ring = fluorStrip(h, (fract(q.y / 1.8) - 0.5) * 1.8, 0.05, top + 0.03);
    float d = rope;
    float paint = 0.381966 * mod(ix + 2.0 * iz, 8.0);
    if (ring < d) {
        d = ring;
        paint = 0.5;
    }
    return vec2(fluorSmin(h, d, 0.03), paint);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    gFluorStride = 0.75;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.2);
}
