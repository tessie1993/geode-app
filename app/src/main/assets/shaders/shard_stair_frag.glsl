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

// Shard Stair: a flight up the middle of a spiral stair of broken glass
// plates with no column, eight to a turn and each half a unit further along
// the way than the last, the whole stair turning slowly about the way.
//
// Morph turns every plate on its own length, from flat treads to blades
// standing edge on. Worst case 100 steps and 3 faces, no noise.

/** Plate j of the turn (j * 45 degrees round), the one of its column of plates nearest p. */
vec3 tread(vec3 p, float j) {
    float m = floor((p.z - 0.5 * j) * 0.25 + 0.5);
    float h = shardHash(vec2(mod(j + 8.0 * m, 32.0), 11.0));
    vec3 q = vec3(shardRot(-j * SHARD_TAU / 8.0) * p.xy, p.z - 0.5 * j - 4.0 * m);
    q.x -= 1.65;
    q.yz = shardRot(gShardForm * 1.3 + 0.6 * (h - 0.5)) * q.yz;
    float edge;
    float d = shardPiece(q, h, 0.6, 0.03, edge);
    return vec3(d, h, edge);
}

vec3 shardMap(vec3 p) {
    p.xy = shardRot(uTime * SHARD_RATE(40)) * p.xy;
    vec2 dir = dot(p.xy, p.xy) > 1.0e-8 ? p.xy : vec2(1.0, 0.0);
    float a = atan(dir.y, dir.x) * 8.0 / SHARD_TAU;
    float j = floor(a + 0.5);
    // A plate reaches past its own eighth of the turn, so the nearer neighbour is asked too.
    vec3 m0 = tread(p, j);
    vec3 m1 = tread(p, j + (a > j ? 1.0 : -1.0));
    return m0.x < m1.x ? m0 : m1;
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    gShardStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = shardFinish(shardTrace(shardEye(ro, 0.4), rd), 1.0);
}
