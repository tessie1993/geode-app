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

// Shard Ring: a ring of broken glass in orbit about an empty middle, seen a
// little from above and turned about. Four bands of splinters, more to each
// band outward; the inner bands go round faster than the outer, and every
// splinter tumbles as it goes.
//
// Morph scatters the ring from one flat sheet into a thick cloud.
//
// Formula, three orbits (lib_strata): 0 the one ring of splinters, as it
// always was; 0.5 every splinter folded by a kaleidoscope in its own plane
// into a snowflake of smaller ones, gStrataIters folds; 1 an armillary, the
// ring mirrored into the two planes square to it, three rings crossing. The
// far fractal follows: a Mandelbox, an Apollonian ball, a kaleidoscope dust.
//
// Worst case 100 steps and 3 faces, one splinter and at most 3 folds a step,
// no noise; then the layers.

/** The ring at p, its splinters folded (fold over 0) or one splinter each, as always. */
vec3 ringAt(vec3 p, float fold) {
    float r = length(p.xz);
    float b = clamp(floor((r - 1.6) / 0.8), 0.0, 3.0);
    float n = 12.0 + 4.0 * b;
    float mid = 2.0 + 0.8 * b;
    vec2 dir = r > 1.0e-4 ? p.xz : vec2(1.0, 0.0);
    // The band's orbit, whole cells per uTime wrap, so the splinters keep their places across it.
    float a = (atan(dir.y, dir.x) + uTime * SHARD_RATE(70.0 - 12.0 * b)) * n / SHARD_TAU;
    float k = floor(a);
    float fa = a - k;
    float h = shardHash(vec2(mod(k, n), b + 3.0));
    float al = (fa - 0.5) * SHARD_TAU / n;
    float spread = 0.8 * gShardForm;
    vec3 q = vec3(r * cos(al) - mid, p.y - spread * (2.0 * h - 1.0), r * sin(al));
    q.y -= 0.06 * sin(uTime * SHARD_RATE(400) + SHARD_TAU * h);
    q.xy = shardRot(SHARD_TAU * h + uTime * SHARD_RATE(150.0 + 100.0 * floor(3.0 * h))) * q.xy;
    q.yz = shardRot(SHARD_TAU * fract(4.3 * h) + uTime * SHARD_RATE(110.0 + 70.0 * floor(3.0 * fract(2.7 * h)))) * q.yz;
    float size = 0.16 * (0.8 + 0.4 * fract(3.3 * h));
    vec2 piece = fold < 0.0 ? shardFolded(q, -1.0, 0.0, 1.0, vec3(0.0), h, size, 0.012)
        : shardFolded(q, 0.0, gStrataIters, 0.6 * size, vec3(1.6, 0.8, 0.0), h, 0.7, 0.03);
    float d = piece.x;
    float edge = piece.y;
    // Every splinter keeps 0.1 inside its own cell, and none lies far above or below the ring.
    float side = r * sin(min(fa, 1.0 - fa) * SHARD_TAU / n);
    float bound = min(side, abs(0.4 - abs(r - mid))) + 0.1;
    float layer = abs(p.y) - 0.4 - spread;
    return vec3(min(d, max(bound, layer)), h, edge);
}

vec3 shardMap(vec3 p) {
    p.yz = shardRot(0.42) * p.yz;
    vec3 a = vec3(1.0e3);
    vec3 b = a;
    vec3 c = a;
    if (strataUses(0)) a = ringAt(p, -1.0);
    if (strataUses(1)) b = ringAt(p, 0.0);
    if (strataUses(2)) {
        // The armillary: the plane nearest p taken as the ring's, the other two coordinates kept signed,
        // so the orbit runs the same way round in each ring.
        vec3 m = abs(p);
        vec3 q = m.x < m.y && m.x < m.z ? vec3(p.z, p.x, p.y) : (m.z < m.y ? vec3(p.x, p.z, -p.y) : p);
        c = ringAt(q, -1.0);
    }
    return strataBlend(a, b, c);
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    strataSetup(vec3(1.0, 2.0, 0.0), 0.0, 0.5, 0.8);
    gShardFar = 14.0 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 7.5, CAM_SPIN_UNIT, ro, rd);
    fragColor = shardFinish(shardTrace(ro, rd), 1.3);
}
