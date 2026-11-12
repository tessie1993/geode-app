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

// Shard Panes: a flight through a stack of broken window panes, one every two
// units of the way, each leaning and turning its own way. A hole is broken out
// of each pane round the way ahead, a crack runs out from every corner of the
// hole, and the pane's outer edge is broken too, so each pane is a ring of
// seven splinters.
//
// Morph breaks the holes wider, from a narrow gap to a thin ring of glass.
// Worst case 100 steps and 3 faces, no noise.

/** How far q lies outside the side from corner a to corner b, taken anticlockwise (negative on the middle's side). */
float paneSide(vec2 q, vec2 a, vec2 b) {
    vec2 e = b - a;
    return dot(q - a, vec2(e.y, -e.x)) / max(length(e), 1.0e-4);
}

vec3 shardMap(vec3 p) {
    float n = floor(p.z * 0.5);
    float seed = mod(n, 8.0);
    float h = shardHash(vec2(seed, 3.7));
    float spin = mod(n, 2.0) * 2.0 - 1.0;
    vec3 q = vec3(shardRot(SHARD_TAU * h + spin * uTime * SHARD_RATE(90)) * p.xy, p.z - 2.0 * n - 1.0);
    // Every pane stays within 0.3 of its own middle along the way, so the next is never nearer than this.
    float wall = 1.0 - abs(q.z) + 0.7;
    float slab = abs(q.z - 0.08 * (fract(5.3 * h) - 0.5) * q.x - 0.08 * (fract(9.1 * h) - 0.5) * q.y) - 0.025;
    float r = length(q.xy);
    vec2 dir = r > 1.0e-4 ? q.xy : vec2(1.0, 0.0);
    float a = atan(dir.y, dir.x) * 7.0 / SHARD_TAU;
    float k = floor(a);
    float fa = a - k;
    // The two corners either side of q, on the hole and on the outline.
    vec2 c0 = vec2(cos(k * SHARD_TAU / 7.0), sin(k * SHARD_TAU / 7.0));
    vec2 c1 = vec2(cos((k + 1.0) * SHARD_TAU / 7.0), sin((k + 1.0) * SHARD_TAU / 7.0));
    float h0 = shardHash(vec2(mod(k, 7.0), seed));
    float h1 = shardHash(vec2(mod(k + 1.0, 7.0), seed));
    float hole = mix(0.75, 1.6, gShardForm);
    float inner = paneSide(q.xy, c0 * hole * (0.7 + 0.6 * h0), c1 * hole * (0.7 + 0.6 * h1));
    float outer = paneSide(q.xy, c0 * (2.8 + fract(7.3 * h0)), c1 * (2.8 + fract(7.3 * h1)));
    float crack = r * sin(min(fa, 1.0 - fa) * SHARD_TAU / 7.0);
    vec2 top = vec2(-1.0e3);
    shardTop(top, slab);
    shardTop(top, -inner);
    shardTop(top, outer);
    shardTop(top, 0.012 + 0.03 * gShardForm - crack);
    return vec3(min(top.x, wall), fract(h + 0.37 * mod(k, 7.0)), length(top));
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    gShardStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = shardFinish(shardTrace(shardEye(ro, 0.35), rd), 1.0);
}
