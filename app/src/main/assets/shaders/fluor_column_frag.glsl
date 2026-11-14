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
//#include lib_strata
//#include lib_fluor

// Fluor Column: a flight down a colonnade in the dark, rows of carved columns
// to either side rising and falling out of sight. Two ropes wind up every
// shaft, against the ones on the next, and banded rings cross them; the ropes
// climb slowly as they turn.
//
// Morph embosses the shafts: the ropes and rings stand further out.
//
// Formula, three colonnades (lib_strata): 0 the ropes, as they always were;
// 0.5 between the rings every shaft carved in Apollonian rings, circles in
// the gaps of circles, four cells round a shaft; 1 a kaleidoscope lattice of
// lozenges within lozenges, the folds turned. Each is gStrataIters folds
// deep, and the cells climb slowly up the shafts as the ropes did. The far
// fractal follows: a Mandelbox, a Menger cube, a kaleidoscope dust, past the
// colonnade.
//
// Worst case 90 steps, at most 3 folds a step, no noise; then the layers.

vec2 colonnade(vec3 p, float kind) {
    float ix = floor((p.x - 1.6) / 3.2 + 0.5);
    float iz = floor(p.z * 0.5);
    vec3 q = vec3(p.x - 1.6 - 3.2 * ix, p.y, p.z - 2.0 * iz - 1.0);
    float h = length(q.xz) - 0.38;
    float side = 2.0 * mod(ix + iz, 2.0) - 1.0;
    float s = 2.0 * side * fluorAngle(q.xz) / FLUOR_TAU + q.y / 0.9 + uTime * FLUOR_RATE(50) / FLUOR_TAU;
    float top = mix(0.04, 0.12, gFluorForm);
    // The rope's spacing across itself, 1 / |grad s| on the shaft.
    float rope = fluorStrip(h, (fract(s) - 0.5) * 0.718, 0.06, top);
    if (kind > 0.5) {
        // Four cells round the shaft, each 0.6 square, climbing as the ropes did.
        float a = 2.0 * fluorAngle(q.xz) / 3.1415927;
        float y = q.y + 0.3 * side * uTime * FLUOR_RATE(50) / FLUOR_TAU;
        vec2 u = vec2((a - floor(a + 0.5)) * 0.597, y - 0.6 * floor(y / 0.6 + 0.5));
        rope = kind < 1.5 ? fluorOrnament(u, h, 2.0, vec2(0.0), 0.0, 0.3, top).x
            : fluorOrnament(u, h, 0.0, vec2(1.3, 0.4), 0.5, 0.3, top).x;
    }
    float ring = fluorStrip(h, (fract(q.y / 1.8) - 0.5) * 1.8, 0.05, top + 0.03);
    float d = rope;
    float paint = 0.381966 * mod(ix + 2.0 * iz, 8.0);
    if (ring < d) {
        d = ring;
        paint = 0.5;
    }
    return vec2(fluorSmin(h, d, 0.03), paint);
}

vec2 fluorMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = colonnade(p, 0.0);
    if (strataUses(1)) b = colonnade(p, 1.0);
    if (strataUses(2)) c = colonnade(p, 2.0);
    return strataBlend2(a, b, c);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    strataSetup(vec3(1.0, 3.0, 0.0), 2.0, 0.4, 0.7);
    gFluorStride = 0.75;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.2);
}
