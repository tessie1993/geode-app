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

// Shard Burst: a ball of glass the moment it shatters, turned about. Its skin
// is fifty-four flat facets, nine to each face of a cube blown out round, the
// cracks between them wavering; each facet is its own piece.
//
// Morph bursts it: from a cracked whole ball to facets flung outward, each as
// far as its own share and tipped out of true. Worst case 100 steps and 3
// faces, no noise.

#define BURST_R 1.5

vec3 shardMap(vec3 p) {
    p.xz = shardRot(uTime * SHARD_RATE(60)) * p.xz;
    p.yz = shardRot(uTime * SHARD_RATE(23)) * p.yz;
    vec3 a = abs(p);
    float l = length(p);
    // The cube face the direction falls on: e0 out through it, e1 and e2 across it.
    vec3 e0;
    vec3 e1;
    vec3 e2;
    float face;
    if (a.x >= a.y && a.x >= a.z) {
        e0 = vec3(p.x >= 0.0 ? 1.0 : -1.0, 0.0, 0.0);
        e1 = vec3(0.0, 1.0, 0.0);
        e2 = vec3(0.0, 0.0, 1.0);
        face = p.x >= 0.0 ? 0.0 : 1.0;
    } else if (a.y >= a.z) {
        e0 = vec3(0.0, p.y >= 0.0 ? 1.0 : -1.0, 0.0);
        e1 = vec3(0.0, 0.0, 1.0);
        e2 = vec3(1.0, 0.0, 0.0);
        face = p.y >= 0.0 ? 2.0 : 3.0;
    } else {
        e0 = vec3(0.0, 0.0, p.z >= 0.0 ? 1.0 : -1.0);
        e1 = vec3(1.0, 0.0, 0.0);
        e2 = vec3(0.0, 1.0, 0.0);
        face = p.z >= 0.0 ? 4.0 : 5.0;
    }
    vec2 uv = vec2(dot(p, e1), dot(p, e2)) / max(dot(p, e0), 1.0e-4);
    // The cracks waver, but never off the cube's own seams.
    uv += 0.1 * sin(uv.yx * 3.7 + face * 1.9) * (1.0 - uv * uv);
    vec2 g = (uv + 1.0) * 1.5;
    vec2 cell = min(floor(g), vec2(2.0));
    vec2 f = g - cell - 0.5;
    // How far to the nearest crack, roughly in world units.
    float crack = (0.5 - max(abs(f.x), abs(f.y))) * l * 0.33;
    float h = shardHash(vec2(face * 9.0 + cell.x * 3.0 + cell.y, 7.0));
    vec2 uc = (cell + 0.5) / 1.5 - 1.0;
    // Under 0.31 off a vector at least 1 long, so never zero.
    vec3 dir = normalize(e0 + uc.x * e1 + uc.y * e2 + gShardForm * 0.35 * (vec3(h, fract(7.7 * h), fract(3.1 * h)) - 0.5));
    float push = gShardForm * (0.25 + 1.1 * h);
    float gap = 0.015 + 0.05 * gShardForm;
    float s = abs(dot(p, dir) - BURST_R - push) - 0.04;
    float k = gap - crack;
    float d = max(s, k);
    // A neighbour's facet lies past its own crack, and no facet lies inside the ball or far outside it.
    float reach = max(BURST_R - 0.05 - l, l - 4.2);
    d = min(d, max(crack + gap, reach));
    return vec3(d, h, length(vec2(s, k)));
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    gShardStride = 0.6;
    vec3 ro;
    vec3 rd;
    camAround(uv, 6.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = shardFinish(shardTrace(ro, rd), 1.2);
}
