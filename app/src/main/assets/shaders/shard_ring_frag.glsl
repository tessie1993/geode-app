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
//#include lib_shard

// Shard Ring: a ring of broken glass in orbit about an empty middle, seen a
// little from above and turned about. Four bands of splinters, more to each
// band outward; the inner bands go round faster than the outer, and every
// splinter tumbles as it goes.
//
// Morph scatters the ring from one flat sheet into a thick cloud. Worst case
// 100 steps and 3 faces, no noise.

vec3 shardMap(vec3 p) {
    p.yz = shardRot(0.42) * p.yz;
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
    float edge;
    float d = shardPiece(q, h, 0.16 * (0.8 + 0.4 * fract(3.3 * h)), 0.012, edge);
    // Every splinter keeps 0.1 inside its own cell, and none lies far above or below the ring.
    float side = r * sin(min(fa, 1.0 - fa) * SHARD_TAU / n);
    float bound = min(side, abs(0.4 - abs(r - mid))) + 0.1;
    float layer = abs(p.y) - 0.4 - spread;
    return vec3(min(d, max(bound, layer)), h, edge);
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    gShardFar = 14.0 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 7.5, CAM_SPIN_UNIT, ro, rd);
    fragColor = shardFinish(shardTrace(ro, rd), 1.3);
}
