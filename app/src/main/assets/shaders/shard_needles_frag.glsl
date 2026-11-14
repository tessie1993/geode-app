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
//#include lib_shard

// Shard Needles: a burst of twenty-six glass needles from one cut core, turned
// about. Each needle is four-sided and tapers to a point; they stand out along
// the cube's axes, its edges' middles and its corners, three lengths of them,
// each breathing on its own clock.
//
// Morph draws the needles out, from short buds on the core to long spines.
//
// Formula, three bursts (lib_strata's folds, the last fold blended in, so a
// cluster grows out of the one burst as Detail or k1 TEXTURE rise): 0 the one
// burst, as it always was; 0.5 a kaleidoscope, the burst mirrored into a
// ring of smaller bursts and those into smaller still; 1 a Menger fold,
// twenty bursts a third the size on the corners and edges of a cube, and
// twenty more inside each. The far fractal follows: a Menger cube, a
// Mandelbox, a kaleidoscope dust.
//
// Worst case 100 steps and 3 faces, one burst and at most 2 folds a step, no
// noise; then the layers.

/** A needle along u from the middle, its sides square to v and w, `len` long and `wid` wide at its root: x the distance, y the edge's. */
vec2 needle(vec3 q, vec3 u, vec3 v, vec3 w, float len, float wid) {
    float s = dot(q, u);
    float taper = wid * (1.0 - s / len);
    vec2 top = vec2(-1.0e3);
    shardTop(top, abs(dot(q, v)) - taper);
    shardTop(top, abs(dot(q, w)) - taper);
    shardTop(top, 0.3 - s);
    return vec2(top.x, length(top));
}

/** best, or the needle n (x its distance, y its edge's) as piece `id` where it is nearer: the branch-free `if (n.x < best.x)`. */
vec3 nearer(vec3 best, vec2 n, float id) {
    return mix(best, vec3(n.x, id, n.y), 1.0 - step(best.x, n.x));
}

/** The burst of needles in its own turned frame: x the distance, y the piece's number, z the edge's. */
vec3 urchin(vec3 p) {
    vec3 q = abs(p);
    // Which of the eight mirrored copies p is in, so a needle and its mirror differ.
    float side = dot(step(vec3(0.0), p), vec3(0.13, 0.29, 0.53));
    float grow = mix(0.9, 2.6, gShardForm);
    // The core: a cube with its corners cut.
    vec2 core = vec2(-1.0e3);
    shardTop(core, q.x - 0.45);
    shardTop(core, q.y - 0.45);
    shardTop(core, q.z - 0.45);
    shardTop(core, dot(q, vec3(0.5773503)) - 0.62);
    vec3 best = vec3(core.x, side, length(core));
    vec2 n = needle(q, vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0),
        grow * (1.0 + 0.08 * sin(uTime * SHARD_RATE(300))), 0.1);
    best = nearer(best, n, fract(side + 0.11));
    n = needle(q, vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
        grow * (1.0 + 0.08 * sin(uTime * SHARD_RATE(340) + 1.0)), 0.1);
    best = nearer(best, n, fract(side + 0.23));
    n = needle(q, vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0),
        grow * (1.0 + 0.08 * sin(uTime * SHARD_RATE(380) + 2.0)), 0.1);
    best = nearer(best, n, fract(side + 0.37));
    n = needle(q, vec3(0.7071068, 0.7071068, 0.0), vec3(-0.7071068, 0.7071068, 0.0), vec3(0.0, 0.0, 1.0),
        0.75 * grow * (1.0 + 0.1 * sin(uTime * SHARD_RATE(420) + 3.0)), 0.08);
    best = nearer(best, n, fract(side + 0.49));
    n = needle(q, vec3(0.7071068, 0.0, 0.7071068), vec3(-0.7071068, 0.0, 0.7071068), vec3(0.0, 1.0, 0.0),
        0.75 * grow * (1.0 + 0.1 * sin(uTime * SHARD_RATE(460) + 4.0)), 0.08);
    best = nearer(best, n, fract(side + 0.61));
    n = needle(q, vec3(0.0, 0.7071068, 0.7071068), vec3(0.0, -0.7071068, 0.7071068), vec3(1.0, 0.0, 0.0),
        0.75 * grow * (1.0 + 0.1 * sin(uTime * SHARD_RATE(500) + 5.0)), 0.08);
    best = nearer(best, n, fract(side + 0.73));
    n = needle(q, vec3(0.5773503), vec3(0.7071068, -0.7071068, 0.0), vec3(0.4082483, 0.4082483, -0.8164966),
        0.9 * grow * (1.0 + 0.12 * sin(uTime * SHARD_RATE(540) + 6.0)), 0.12);
    best = nearer(best, n, fract(side + 0.87));
    return best;
}

/** The burst seen through fold k (lib_strata), `iters` folds in units of `span`. */
vec3 urchinFolded(vec3 p, float k, float iters, float span, vec3 c) {
    float dr = 1.0 / span;
    vec3 q = strataFold(p * dr, k, iters, c, shardRot(0.6), dr);
    vec3 m = urchin(q);
    return vec3(m.x / dr, m.y, m.z / dr);
}

vec3 shardMap(vec3 p) {
    p.xz = shardRot(uTime * SHARD_RATE(50)) * p.xz;
    p.xy = shardRot(uTime * SHARD_RATE(31)) * p.xy;
    vec3 a = vec3(1.0e3);
    vec3 b = a;
    vec3 c = a;
    if (strataUses(0)) a = urchin(p);
    if (strataUses(1)) b = urchinFolded(p, 0.0, gStrataIters - 1.0, 1.4, vec3(2.6, 1.2, 0.0));
    if (strataUses(2)) c = urchinFolded(p, 3.0, gStrataIters - 1.0, 1.3, vec3(1.0));
    return strataBlend(a, b, c);
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    strataSetup(vec3(3.0, 1.0, 0.0), 2.0, 0.5, 0.7);
    gShardStride = 0.7;
    vec3 ro;
    vec3 rd;
    camAround(uv, 6.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = shardFinish(shardTrace(ro, rd), 1.2);
}
