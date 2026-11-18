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

// Fluor Lattice: a flight through pierced screens that stand across the way,
// one every two units. Each is a lattice of bars studded at every crossing,
// round an opening ringed by a heavier moulding, and each is turned from the
// last, so the screens behind show through the holes as a shifting weave.
//
// Morph fills the lattice: the bars and studs thicken and the opening draws
// in.
//
// Formula, three screens (lib_strata): 0 the bars and studs, as they always
// were; 0.5 a Menger screen, bars round squares within squares; 1 an
// Apollonian screen, bars bent round circles nested in the gaps between
// circles. Each is gStrataIters folds deep and turned with its screen, so
// the screens behind show through as a shifting weave at every scale. The
// far fractal follows: a Menger cube, an Apollonian ball, a kaleidoscope
// dust, beyond the last screen.
//
// Worst case 90 steps, at most 3 folds a step, no noise; then the layers.

vec2 screens(vec3 p, float kind) {
    float k = floor(p.z * 0.5 + 0.5);
    float z = p.z - 2.0 * k;
    float r = length(p.xy);
    vec2 u = fluorRot(0.7853982 + 0.3926991 * mod(k, 8.0)) * p.xy;
    vec2 g = (fract(u * 2.5 + 0.5) - 0.5) * 0.4;
    float mouth = mix(1.05, 0.85, gFluorForm);
    float open = mouth - r;
    float bars = fluorSlab(max(min(abs(g.x), abs(g.y)) - mix(0.025, 0.065, gFluorForm), open), z, 0.04);
    float stud = max(length(vec3(g, z)) - (0.06 + 0.03 * gFluorForm), open);
    if (kind > 0.5) {
        // Bars round the outlines of a fold, one cell to every 0.8 square of screen.
        vec2 w = u - 0.8 * floor(u / 0.8 + 0.5);
        float dr = 2.5;
        vec2 f = strataFold2(w * dr, kind < 1.5 ? 3.0 : 2.0, gStrataIters, vec2(1.0), fluorRot(0.0), dr);
        float outline = (kind < 1.5 ? abs(max(abs(f.x), abs(f.y)) - 0.6) : abs(length(f) - 0.8)) / dr;
        bars = fluorSlab(max(outline - mix(0.015, 0.04, gFluorForm), open), z, 0.04);
        stud = 1.0e3;
    }
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

vec2 fluorMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = screens(p, 0.0);
    if (strataUses(1)) b = screens(p, 1.0);
    if (strataUses(2)) c = screens(p, 2.0);
    return strataBlend2(a, b, c);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    strataSetup(vec3(3.0, 2.0, 0.0), 2.0, 0.4, 0.7);
    gFluorStride = 0.75;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.0);
}
