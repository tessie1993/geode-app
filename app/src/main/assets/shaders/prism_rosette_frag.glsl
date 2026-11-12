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
//#include lib_prism

// Prism Rosette: a flight past chrome mandalas hung in the dark, one to a cell
// of a lattice, each at its own depth and size and turning slowly on its own
// axis. A disc's rim is cut into eight lotus scallops, a ring of round holes
// pierces it, and its face is turned in fine rings; lines of light run out
// from its centre along every mirror of its fold and round every hole.
//
// Morph tips the discs from facing the way toward edge-on.
//
// Formula, three mandalas (lib_strata): 0 the discs, as they always were;
// 0.5 rose windows: each disc's face folded by a kaleidoscope gStrataIters
// times, pierced by rings of round holes within rings at every scale; 1
// gyroscopes: three discs to a cell, square to each other, turning as one.
// The far fractal follows: a Menger cube, a Mandelbox, a kaleidoscope dust.
//
// Worst case 108 steps, one fold and at most 3 kaleidoscope folds, or three
// discs, a step, no noise; then the layers.

#define ROSE_N 8.0

/** One disc in its own frame l, radius rad: x the distance, y the ribbon's; its face a rose window when `rose`. */
vec2 rosetteDisc(vec3 l, float rad, float rose) {
    vec2 q = prismFold(l.xy, ROSE_N);
    float r = length(l.xy);
    float rim = rad * (0.82 + 0.18 * cos(ROSE_N * prismAngle(q)));
    float hole = length(q - vec2(0.56 * rad, 0.0)) - 0.12 * rad;
    if (rose > 0.5) {
        // Rings of holes within rings: the face folded, a hole at each fold's corner.
        float dr = 1.0 / rad;
        vec2 f = strataFold2(l.xy * dr, 0.0, gStrataIters, vec2(1.1, 0.45), prismRot(PRISM_PI / ROSE_N), dr);
        hole = min(hole, (length(f) - 0.3) / dr);
    }
    float face = max(r - rim, -hole);
    float th = 0.035 + 0.015 * cos(18.0 * r);
    vec2 w = vec2(face, abs(l.z) - th);
    float d = min(max(w.x, w.y), 0.0) + length(max(w, 0.0));
    return vec2(d, min(min(q.y, prismEdge(q, ROSE_N)), abs(hole)));
}

vec2 prismMap(vec3 p) {
    // Cells four units wide, the way ahead along their corners.
    vec3 c = floor(p * 0.25);
    vec3 l = p - 4.0 * c - 2.0;
    vec3 a = abs(l);
    // Nothing here reaches past 1.65 of its cell's middle, so the next cell's disc is never nearer than this.
    float bound = 2.35 - max(a.x, max(a.y, a.z));
    float h = prismHash(vec3(c.xy, mod(c.z, 4.0)));
    float h2 = fract(h * 57.31);
    float h3 = fract(h * 13.7);
    l -= vec3(0.4 * (h - 0.5), 0.4 * (h2 - 0.5), 0.8 * (h3 - 0.5));
    l.yz = prismRot(mix(0.15, 1.2, gPrismForm) * (2.0 * h - 1.0)) * l.yz;
    l.xy = prismRot(PRISM_TAU * h +(h3 > 0.5 ? 1.0 : -1.0) * uTime * PRISM_RATE(50)) * l.xy;
    float rad = 0.85 + 0.4 * h2;
    vec2 m0 = vec2(1.0e3);
    vec2 m1 = m0;
    vec2 m2 = m0;
    if (strataUses(0)) m0 = rosetteDisc(l, rad, 0.0);
    if (strataUses(1)) m1 = rosetteDisc(l, rad, 1.0);
    if (strataUses(2)) {
        m2 = rosetteDisc(l, rad, 0.0);
        vec2 t = rosetteDisc(l.zxy, 0.8 * rad, 0.0);
        m2 = t.x < m2.x ? t : m2;
        t = rosetteDisc(l.yzx, 0.65 * rad, 0.0);
        m2 = t.x < m2.x ? t : m2;
    }
    vec2 m = strataBlend2(m0, m1, m2);
    return vec2(min(m.x, bound), m.y);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    strataSetup(vec3(3.0, 1.0, 0.0), 2.0, 0.45, 0.7);
    gPrismStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.15);
}
