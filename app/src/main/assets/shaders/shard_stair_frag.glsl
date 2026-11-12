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

// Shard Stair: a flight up the middle of a spiral stair of broken glass
// plates with no column, eight to a turn and each half a unit further along
// the way than the last, the whole stair turning slowly about the way.
//
// Morph turns every plate on its own length, from flat treads to blades
// standing edge on.
//
// Formula, three stairs (lib_strata): 0 the plates, as they always were; 0.5
// every plate cut by a Menger fold in its own plane, a frost of squares
// within squares, gStrataIters folds; 1 a stair within the stair, a second
// spiral half the size turning the other way inside the first. The far
// fractal follows: a Mandelbox, a Menger cube, a kaleidoscope dust.
//
// Worst case 100 steps and 3 faces, four plates and at most 3 folds a step,
// no noise; then the layers.

/** Plate j of the turn (j * 45 degrees round), the one of its column of plates nearest p; frosted by a Menger fold when `frost`. */
vec3 tread(vec3 p, float j, bool frost) {
    float m = floor((p.z - 0.5 * j) * 0.25 + 0.5);
    float h = shardHash(vec2(mod(j + 8.0 * m, 32.0), 11.0));
    vec3 q = vec3(shardRot(-j * SHARD_TAU / 8.0) * p.xy, p.z - 0.5 * j - 4.0 * m);
    q.x -= 1.65;
    q.yz = shardRot(gShardForm * 1.3 + 0.6 * (h - 0.5)) * q.yz;
    vec2 piece = frost ? shardFolded(q, 3.0, gStrataIters - 0.5, 0.62, vec3(1.0, 1.0, 0.0), h, 1.0, 0.05)
        : shardFolded(q, -1.0, 0.0, 1.0, vec3(0.0), h, 0.6, 0.03);
    return vec3(piece.x, h, piece.y);
}

/** The stair at p, turned `spin` about the way. */
vec3 stair(vec3 p, float spin, bool frost) {
    p.xy = shardRot(spin) * p.xy;
    vec2 dir = dot(p.xy, p.xy) > 1.0e-8 ? p.xy : vec2(1.0, 0.0);
    float a = atan(dir.y, dir.x) * 8.0 / SHARD_TAU;
    float j = floor(a + 0.5);
    // A plate reaches past its own eighth of the turn, so the nearer neighbour is asked too.
    vec3 m0 = tread(p, j, frost);
    vec3 m1 = tread(p, j + (a > j ? 1.0 : -1.0), frost);
    return m0.x < m1.x ? m0 : m1;
}

vec3 shardMap(vec3 p) {
    float spin = uTime * SHARD_RATE(40);
    vec3 a = vec3(1.0e3);
    vec3 b = a;
    vec3 c = a;
    if (strataUses(0) || strataUses(2)) a = stair(p, spin, false);
    if (strataUses(1)) b = stair(p, spin, true);
    if (strataUses(2)) {
        // The inner stair: half the size (so twice as many turns along the way, still whole in 16), the other way round.
        vec3 m = stair(2.0 * p, -2.0 * spin + 0.4, false);
        m.x *= 0.5;
        m.z *= 0.5;
        c = m.x < a.x ? m : a;
    }
    return strataBlend(a, b, c);
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    strataSetup(vec3(1.0, 3.0, 0.0), 2.0, 0.5, 0.8);
    gShardStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = shardFinish(shardTrace(shardEye(ro, 0.4), rd), 1.0);
}
