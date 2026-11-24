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

// Fluor Honeycomb: a flight through a low cavern whose floor and roof roll in
// long swells, carved all over in a honeycomb: raised six-sided walls, and a
// lower ring standing in the middle of every cell.
//
// Morph raises the walls and the rings.
//
// Formula, three combs (lib_strata): 0 a ring in every cell, as it always
// was; 0.5 every cell filled with Apollonian rings, circles nested in the
// gaps between circles; 1 every cell a kaleidoscope star of lozenges within
// lozenges, the folds turned a twelfth. Each is gStrataIters folds deep,
// the finer frames narrower and lower. The far fractal follows: a
// kaleidoscope dust, a Mandelbox, an Apollonian ball, past the cavern's
// swells.
//
// Worst case 90 steps, at most 3 folds a step, no noise; then the layers.

vec2 comb(vec3 p, float kind) {
    // The floor and the roof, eight units to a swell along the way.
    float hf = p.y + 1.05 - 0.3 * sin(0.7853982 * p.z + 0.5 * p.x);
    float hc = 1.05 + 0.3 * sin(0.7853982 * p.z - 0.5 * p.x + 2.0) - p.y;
    float roof = step(hc, hf);
    float h = 0.96 * min(hf, hc);
    // The cells, half a unit apart along the way; the roof's moved half a cell.
    vec2 u = vec2(p.z + 0.25 * roof, p.x);
    vec2 per = vec2(0.5, 0.8660254);
    vec2 ia = floor(u / per + 0.5);
    vec2 a = u - per * ia;
    vec2 ib = floor((u - 0.5 * per) / per + 0.5);
    vec2 b = u - 0.5 * per - per * ib;
    bool first = dot(a, a) < dot(b, b);
    vec2 q = first ? a : b;
    vec2 id = first ? ia : ib;
    // Distance to the cell's border: its six sides are a quarter unit from the middle.
    float hex = max(abs(q.x), max(abs(dot(q, vec2(0.5, 0.8660254))), abs(dot(q, vec2(0.5, -0.8660254)))));
    float top = mix(0.04, 0.13, gFluorForm);
    float wall = fluorStrip(h, 0.25 - hex, 0.025, top);
    float ring = fluorStrip(h, length(q) - 0.12, 0.015, 0.45 * top);
    if (kind > 0.5) {
        // The cell's own ornament, kept inside its walls.
        vec2 orn = kind < 1.5 ? fluorOrnament(q, h, 2.0, vec2(0.0), 0.0, 0.14, 0.6 * top)
            : fluorOrnament(q, h, 0.0, vec2(1.25, 0.45), 0.5235988, 0.14, 0.6 * top);
        ring = max(orn.x, hex - 0.2);
    }
    float d = wall;
    float paint = 0.381966 * mod(id.x + 3.0 * id.y, 8.0) + (first ? 0.0 : 0.19);
    if (ring < d) {
        d = ring;
        paint += 0.5;
    }
    return vec2(fluorSmin(h, d, 0.03), paint);
}

vec2 fluorMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = comb(p, 0.0);
    if (strataUses(1)) b = comb(p, 1.0);
    if (strataUses(2)) c = comb(p, 2.0);
    return strataBlend2(a, b, c);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    strataSetup(vec3(0.0, 1.0, 2.0), 1.0, 0.4, 0.7);
    gFluorStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.0);
}
