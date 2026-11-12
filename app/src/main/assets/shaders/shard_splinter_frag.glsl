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

// Shard Splinter: the family's opener. A flight through a drift of broken
// glass: two lattices of cells, the second offset half a cell from the first,
// each cell holding one cluster of splinters that bobs and tumbles on its own
// two clocks. Behind the drift a far lattice of fractals glows (lib_strata),
// and a warped haze hangs between.
//
// Formula, three ways to break a pane (lib_strata's folds in the pane's own
// plane, gStrataIters of them, so each stays a pane): 0 one splinter, as it
// always was; 0.5 a kaleidoscope, the splinter mirrored into a snowflake of
// smaller ones; 1 a Menger cut, a frost of squares within squares. The far
// fractal follows: a kaleidoscope dust, an Apollonian ball, a Menger cube.
//
// Morph grows the splinters from slivers to broad panes. Worst case 100 steps
// and 3 faces, two clusters of at most 3 folds a step (none where a ray is
// far from a cluster), no noise; then the layers (lib_strata).

/**
 * The splinter in its own frame, by formula k: 0 one pane; 1 the pane folded by a kaleidoscope in
 * its own plane, a snowflake of smaller splinters; 2 the pane cut by a Menger fold in its plane, a
 * frost of squares within squares. Every fold keeps z = 0, so the cluster stays a pane.
 */
vec2 splinterPane(vec3 q, float k, float h, float size) {
    if (k < 0.5) return shardFolded(q, -1.0, 0.0, 1.0, vec3(0.0), h, size, 0.02);
    if (k < 1.5) return shardFolded(q, 0.0, gStrataIters, 0.55 * size, vec3(1.6, 0.8, 0.0), h, 0.7, 0.05);
    return shardFolded(q, 3.0, gStrataIters - 0.5, 1.05 * size, vec3(1.0, 1.0, 0.0), h, 1.0, 0.04);
}

vec3 splinter(vec3 p, vec3 off) {
    vec3 c = floor((p + off) * 0.25);
    vec3 q = p + off - 4.0 * c - 2.0;
    float h = shardHash(c.xy + 17.0 * mod(c.z, 4.0) + 5.3 * off.x);
    // Every cluster keeps 0.55 inside its own cell, so a neighbour's is never nearer than this one's wall.
    float wall = 2.0 - max(abs(q.x), max(abs(q.y), abs(q.z)));
    // Far from its own cluster a step needs only the ball round it: the folds are not paid for there.
    float ball = length(q) - 1.45;
    if (ball > 0.25) return vec3(min(ball, wall + 0.55), h, 1.0);
    q.y += 0.15 * sin(uTime * SHARD_RATE(700) + SHARD_TAU * h);
    q.xz = shardRot(SHARD_TAU * h + uTime * SHARD_RATE(120.0 + 80.0 * floor(4.0 * h))) * q.xz;
    q.yz = shardRot(SHARD_TAU * fract(7.1 * h) - uTime * SHARD_RATE(90.0 + 60.0 * floor(3.0 * fract(3.3 * h)))) * q.yz;
    float size = mix(0.45, 0.85, gShardForm) * (0.75 + 0.25 * fract(5.7 * h));
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 e = a;
    if (strataUses(0)) a = splinterPane(q, 0.0, h, size);
    if (strataUses(1)) b = splinterPane(q, 1.0, h, size);
    if (strataUses(2)) e = splinterPane(q, 2.0, h, size);
    vec2 m = strataBlend2(a, b, e);
    return vec3(min(max(m.x, ball), wall + 0.55), h, m.y);
}

vec3 shardMap(vec3 p) {
    vec3 a = splinter(p, vec3(0.0));
    vec3 b = splinter(p, vec3(2.0));
    return a.x < b.x ? a : b;
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    strataSetup(vec3(0.0, 2.0, 3.0), 0.0, 0.6, 0.8);
    gShardStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, SHARD_PERIOD);
    fragColor = shardFinish(shardTrace(ro, rd), 1.1);
}
