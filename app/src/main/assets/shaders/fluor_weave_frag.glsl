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

// Fluor Weave: a flight down a cleft between two tall walls carved as
// basketwork, strips running along the way and strips running up, each
// passing over one and under the next, so every crossing shows a glowing
// outline lying across another.
//
// Morph works the weave: the strips rise and dip further over and under.
//
// Formula, three walls (lib_strata): 0 the basketwork, as it always was;
// 0.5 Apollonian tracery, frames bent round circles nested in the gaps
// between circles, a cell to every unit of wall; 1 a kaleidoscope trellis,
// lozenges within lozenges, the folds turned an eighth. Each is
// gStrataIters folds deep, the right wall's set half a cell higher. The far
// fractal follows: a Mandelbox, an Apollonian ball, a Menger cube, down the
// cleft.
//
// Worst case 90 steps, at most 3 folds a step, no noise; then the layers.

/** The walls carved with lib_fluor's ornament of fold `kind`. */
vec2 tracery(vec3 p, float kind, vec2 c, float turn, float span) {
    float right = step(0.0, p.x);
    float h = 1.3 - abs(p.x);
    vec2 u = vec2(p.z, p.y + 0.5 * right);
    vec2 cell = floor(u + 0.5);
    vec2 orn = fluorOrnament(u - cell, h, kind, c, turn, span, mix(0.06, 0.14, gFluorForm));
    return vec2(fluorSmin(h, orn.x, 0.02), 0.381966 * mod(cell.y + 2.0 * right, 5.0) + orn.y);
}

vec2 weave(vec3 p) {
    // The right wall's weave sits a quarter strip higher than the left's.
    float right = step(0.0, p.x);
    float h = 1.3 - abs(p.x);
    // In strips: one every half unit, along the way and up the wall.
    vec2 u = 2.0 * vec2(p.z, p.y + 0.25 * right);
    vec2 id = floor(u + 0.5);
    vec2 g = 0.5 * (u - id);
    float dip = mix(0.015, 0.06, gFluorForm);
    float rowTop = 0.08 + dip * cos(3.1415927 * (u.x + id.y));
    float colTop = 0.08 - dip * cos(3.1415927 * (u.y + id.x));
    float row = fluorStrip(h, g.y, 0.2, rowTop);
    float col = fluorStrip(h, g.x, 0.2, colTop);
    float d = row;
    float paint = 0.381966 * mod(id.y, 5.0);
    if (col < d) {
        d = col;
        paint = 0.5 + 0.381966 * mod(id.x, 4.0);
    }
    return vec2(fluorSmin(h, d, 0.02), paint);
}

vec2 fluorMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = weave(p);
    if (strataUses(1)) b = tracery(p, 2.0, vec2(0.0), 0.0, 0.25);
    if (strataUses(2)) c = tracery(p, 0.0, vec2(1.2, 0.5), 0.7853982, 0.5);
    return strataBlend2(a, b, c);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    strataSetup(vec3(1.0, 2.0, 3.0), 1.0, 0.4, 0.7);
    gFluorStride = 0.65;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.0);
}
