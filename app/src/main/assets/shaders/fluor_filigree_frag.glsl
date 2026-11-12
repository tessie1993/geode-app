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

// Fluor Filigree: a flight between stacked sheets of lace, three above and
// three below. Each sheet is fine wire bent into a net of rounded loops, a
// thicker boss in every loop and open between, and each bends its own way, so
// the sheets beyond show through as layer under layer of glowing lace.
//
// Morph thickens the wire and the bosses.
//
// Formula, three laces (lib_strata): 0 the net of loops, as it always was;
// 0.5 Apollonian lace, wire bent round circles nested in the gaps between
// circles, a unit cell of it each sheet set its own way over; 1 Menger lace,
// wire round squares within squares. Each is gStrataIters folds deep, the
// finer wire as fine. The far fractal follows: an Apollonian ball, a
// kaleidoscope dust, a Menger cube, through the gap between the sheets.
//
// Worst case 90 steps, at most 3 folds a step, no noise; then the layers.

vec2 lace(vec3 p, float kind) {
    float ay = abs(p.y);
    float j = clamp(floor((ay - 0.7) / 0.8 + 0.5), 0.0, 2.0);
    float y = ay - 0.7 - 0.8 * j;
    float sheet = j + 3.0 * step(0.0, p.y);
    // The lace: a field whose zero lines bend into a net of loops, two to a unit along the way.
    vec2 u = FLUOR_TAU * p.xz;
    float a = u.x + 0.8 * sin(u.y) + 1.3 * sheet;
    float b = u.y + 0.8 * sin(u.x + 2.1 * sheet);
    float f = sin(a) * sin(b);
    vec2 grad = vec2(cos(a) * sin(b) + 0.8 * sin(a) * cos(b) * cos(u.x + 2.1 * sheet),
                     0.8 * cos(a) * sin(b) * cos(u.y) + sin(a) * cos(b));
    // Held under a quarter unit, so the middle of a loop never reads further from its wire than it is.
    float across = 1.0 / (FLUOR_TAU * max(length(grad), 0.6));
    float wire = fluorSlab(abs(f) * across - mix(0.012, 0.035, gFluorForm), y, 0.025);
    float boss = fluorSlab((0.9 - mix(0.05, 0.15, gFluorForm) - abs(f)) * across, y, 0.045);
    if (kind > 0.5) {
        // The wire round the outlines of a fold, a unit cell to each, every sheet moved its own way.
        vec2 w = p.xz + vec2(0.37, 0.61) * sheet;
        w -= floor(w + 0.5);
        float dr = 2.0;
        vec2 g = strataFold2(w * dr, kind < 1.5 ? 2.0 : 3.0, gStrataIters, vec2(1.0), fluorRot(0.3 * sheet), dr);
        float outline = (kind < 1.5 ? abs(length(g) - 0.8) : abs(max(abs(g.x), abs(g.y)) - 0.6)) / dr;
        wire = fluorSlab(outline - mix(0.01, 0.025, gFluorForm), y, 0.025);
        boss = 1.0e3;
    }
    float d = wire;
    float paint = 0.381966 * sheet;
    if (boss < d) {
        d = boss;
        paint += 0.5;
    }
    return vec2(d, paint);
}

vec2 fluorMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = lace(p, 0.0);
    if (strataUses(1)) b = lace(p, 1.0);
    if (strataUses(2)) c = lace(p, 2.0);
    return strataBlend2(a, b, c);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    strataSetup(vec3(2.0, 0.0, 3.0), 0.0, 0.4, 0.7);
    gFluorStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.0);
}
