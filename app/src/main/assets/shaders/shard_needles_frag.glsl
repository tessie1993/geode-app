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

// Shard Needles: a burst of twenty-six glass needles from one cut core, turned
// about. Each needle is four-sided and tapers to a point; they stand out along
// the cube's axes, its edges' middles and its corners, three lengths of them,
// each breathing on its own clock.
//
// Morph draws the needles out, from short buds on the core to long spines.
// Worst case 100 steps and 3 faces, no noise.

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

vec3 shardMap(vec3 p) {
    p.xz = shardRot(uTime * SHARD_RATE(50)) * p.xz;
    p.xy = shardRot(uTime * SHARD_RATE(31)) * p.xy;
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
    if (n.x < best.x) best = vec3(n.x, fract(side + 0.11), n.y);
    n = needle(q, vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
        grow * (1.0 + 0.08 * sin(uTime * SHARD_RATE(340) + 1.0)), 0.1);
    if (n.x < best.x) best = vec3(n.x, fract(side + 0.23), n.y);
    n = needle(q, vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0),
        grow * (1.0 + 0.08 * sin(uTime * SHARD_RATE(380) + 2.0)), 0.1);
    if (n.x < best.x) best = vec3(n.x, fract(side + 0.37), n.y);
    n = needle(q, vec3(0.7071068, 0.7071068, 0.0), vec3(-0.7071068, 0.7071068, 0.0), vec3(0.0, 0.0, 1.0),
        0.75 * grow * (1.0 + 0.1 * sin(uTime * SHARD_RATE(420) + 3.0)), 0.08);
    if (n.x < best.x) best = vec3(n.x, fract(side + 0.49), n.y);
    n = needle(q, vec3(0.7071068, 0.0, 0.7071068), vec3(-0.7071068, 0.0, 0.7071068), vec3(0.0, 1.0, 0.0),
        0.75 * grow * (1.0 + 0.1 * sin(uTime * SHARD_RATE(460) + 4.0)), 0.08);
    if (n.x < best.x) best = vec3(n.x, fract(side + 0.61), n.y);
    n = needle(q, vec3(0.0, 0.7071068, 0.7071068), vec3(0.0, -0.7071068, 0.7071068), vec3(1.0, 0.0, 0.0),
        0.75 * grow * (1.0 + 0.1 * sin(uTime * SHARD_RATE(500) + 5.0)), 0.08);
    if (n.x < best.x) best = vec3(n.x, fract(side + 0.73), n.y);
    n = needle(q, vec3(0.5773503), vec3(0.7071068, -0.7071068, 0.0), vec3(0.4082483, 0.4082483, -0.8164966),
        0.9 * grow * (1.0 + 0.12 * sin(uTime * SHARD_RATE(540) + 6.0)), 0.12);
    if (n.x < best.x) best = vec3(n.x, fract(side + 0.87), n.y);
    return best;
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    gShardStride = 0.7;
    vec3 ro;
    vec3 rd;
    camAround(uv, 6.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = shardFinish(shardTrace(ro, rd), 1.2);
}
