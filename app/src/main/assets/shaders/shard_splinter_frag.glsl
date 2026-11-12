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

// Shard Splinter: the family's opener. A flight through a drift of broken
// glass: two lattices of cells, the second offset half a cell from the first,
// each cell holding one splinter of pane that bobs and tumbles on its own two
// clocks.
//
// Morph grows the splinters from slivers to broad panes. Worst case 100 steps
// and 3 faces, no noise.

vec3 splinter(vec3 p, vec3 off) {
    vec3 c = floor((p + off) * 0.25);
    vec3 q = p + off - 4.0 * c - 2.0;
    float h = shardHash(c.xy + 17.0 * mod(c.z, 4.0) + 5.3 * off.x);
    // Every splinter keeps 0.55 inside its own cell, so a neighbour's is never nearer than this one's wall.
    float wall = 2.0 - max(abs(q.x), max(abs(q.y), abs(q.z)));
    q.y += 0.15 * sin(uTime * SHARD_RATE(700) + SHARD_TAU * h);
    q.xz = shardRot(SHARD_TAU * h + uTime * SHARD_RATE(120.0 + 80.0 * floor(4.0 * h))) * q.xz;
    q.yz = shardRot(SHARD_TAU * fract(7.1 * h) - uTime * SHARD_RATE(90.0 + 60.0 * floor(3.0 * fract(3.3 * h)))) * q.yz;
    float size = mix(0.45, 0.85, gShardForm) * (0.75 + 0.25 * fract(5.7 * h));
    float edge;
    float d = shardPiece(q, h, size, 0.02, edge);
    return vec3(min(d, wall + 0.55), h, edge);
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
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, SHARD_PERIOD);
    fragColor = shardFinish(shardTrace(ro, rd), 1.1);
}
