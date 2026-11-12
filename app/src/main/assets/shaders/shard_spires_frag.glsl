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

// Shard Spires: a flight low over a field of crystal spires, one to a cell,
// each a six-sided column ending in a six-faced point, turned and leaning its
// own way and rising out of the dark below.
//
// Morph grows them from stubs to tall points that reach past the eye. Worst
// case 100 steps and 3 faces, no noise.

#define SPIRE_BASE -2.4

vec3 shardMap(vec3 p) {
    vec2 c = floor(p.xz * 0.5);
    float h = shardHash(c - vec2(0.0, 8.0) * floor(c.y / 8.0));
    vec2 o = p.xz - 2.0 * c - 1.0;
    // Every spire keeps 0.2 inside its own cell, so a neighbour's is never nearer than this one's wall.
    float wall = 1.0 - max(abs(o.x), abs(o.y)) + 0.2;
    vec2 lean = 0.2 * (vec2(fract(2.3 * h), fract(4.7 * h)) - 0.5);
    vec2 xz = o - 0.3 * (vec2(h, fract(9.7 * h)) - 0.5) - lean * (p.y - SPIRE_BASE);
    xz = shardRot(SHARD_TAU * fract(1.9 * h)) * xz;
    float w = 0.16 + 0.12 * fract(6.1 * h);
    float top = -1.3 + mix(0.45, 1.9, gShardForm) * (0.45 + 0.55 * fract(3.9 * h));
    // The hexagon folded into one quarter; the mirrored side is kept too, so the corner on the fold is an edge.
    vec2 a = abs(xz);
    float side = a.x * 0.8660254 + a.y * 0.5;
    float back = a.x * 0.8660254 - a.y * 0.5;
    // The point's faces run from the apex down to the column's sides, 2.6 widths below.
    float rise = p.y - top;
    vec2 tip = vec2(-1.0e3);
    shardTop(tip, side - w);
    shardTop(tip, back - w);
    shardTop(tip, a.y - w);
    shardTop(tip, (2.6 * side + rise) * 0.3589);
    shardTop(tip, (2.6 * back + rise) * 0.3589);
    shardTop(tip, (2.6 * a.y + rise) * 0.3589);
    shardTop(tip, SPIRE_BASE - p.y);
    return vec3(min(tip.x, wall), h, length(tip));
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    gShardStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, SHARD_PERIOD);
    fragColor = shardFinish(shardTrace(ro, rd), 1.1);
}
