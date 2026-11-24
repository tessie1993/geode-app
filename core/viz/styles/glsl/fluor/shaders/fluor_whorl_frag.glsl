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

// Fluor Whorl: the family's opener. A flight between a carved floor and
// ceiling laid out in square tiles, each framed by a ridge and filled with one
// raised spiral round a boss, the next tile's spiral wound the other way; the
// spirals turn slowly, so their arms run outward.
//
// Morph embosses the tiles: the spirals stand higher and the bosses swell.
//
// Formula, three pavements (lib_strata): 0 the spirals, as they always were;
// 0.5 every tile filled with Apollonian rings round its boss, circles nested
// in the gaps between circles; 1 every tile a kaleidoscope mandala, lozenges
// within lozenges, the folds turned. Each is gStrataIters folds deep, the
// finer frames narrower and lower; the framing ridges stay. The far fractal
// follows: a kaleidoscope dust, an Apollonian ball, a Menger cube, at the far
// end between floor and ceiling.
//
// Worst case 90 steps, at most 3 folds a step, no noise; then the layers.

vec2 tiles(vec3 p, float kind) {
    // The ceiling's tiles sit half a tile over from the floor's.
    float up = step(0.0, p.y);
    float h = 1.1 - abs(p.y);
    vec2 u = p.xz + 0.5 * up;
    vec2 cell = floor(u);
    vec2 q = u - cell - 0.5;
    float r = length(q);
    float side = 2.0 * mod(cell.x + cell.y, 2.0) - 1.0;
    float s = r / 0.11 - side * (fluorAngle(q) / FLUOR_TAU + uTime * FLUOR_RATE(40) / FLUOR_TAU);
    float top = mix(0.04, 0.11, gFluorForm);
    float spiral = max(fluorStrip(h, (fract(s) - 0.5) * 0.11, 0.022, top), r - 0.4);
    if (kind > 0.5) {
        // The tile's own ornament, kept inside its frame.
        vec2 orn = kind < 1.5 ? fluorOrnament(q, h, 2.0, vec2(0.0), 0.0, 0.2, top)
            : fluorOrnament(q, h, 0.0, vec2(1.3, 0.55), 0.6 * side, 0.4, top);
        spiral = max(orn.x, max(abs(q.x), abs(q.y)) - 0.45);
    }
    float boss = length(vec3(q, h)) - (0.05 + 0.06 * gFluorForm);
    float frame = fluorStrip(h, 0.5 - max(abs(q.x), abs(q.y)), 0.03, 0.6 * top + 0.02);
    float d = min(spiral, boss);
    float paint = 0.381966 * mod(cell.x + 3.0 * cell.y, 8.0);
    if (frame < d) {
        d = frame;
        paint = 0.5;
    }
    return vec2(fluorSmin(h, d, 0.04), paint);
}

vec2 fluorMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = tiles(p, 0.0);
    if (strataUses(1)) b = tiles(p, 1.0);
    if (strataUses(2)) c = tiles(p, 2.0);
    return strataBlend2(a, b, c);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    strataSetup(vec3(0.0, 2.0, 3.0), 2.0, 0.4, 0.8);
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.0);
}
