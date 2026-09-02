#version 300 es
// Rebuilt particle seeding: every particle is born AT a choreographed spawn
// point (weighted pick over uSpawns, jittered), not at a random screen
// position - the population belongs to the spawn choreography from frame
// one. Ages are staggered across the lifetime so recycling never
// synchronizes into visible waves. MRT: state A = (pos.xy, vel.xy),
// state B = (age, ttl, emitterIndex, seed).
precision highp float;
in vec2 vUv;
uniform highp float uAspect;
uniform vec4 uSpawns[8];   // (x, y, weight, jitterRadius) in sim space
uniform int uSpawnCount;
uniform float uLife;       // base lifetime seconds
layout(location = 0) out vec4 outPosVel;
layout(location = 1) out vec4 outMeta;
float hash(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract((p.x + p.y) * p.x);
}
void main() {
    float h0 = hash(vUv);
    float h1 = hash(vUv + 7.31);
    float h2 = hash(vUv + 3.77);
    float h3 = hash(vUv + 11.13);
    // Weighted spawn-point pick: cumulative scan over the active points.
    float total = 0.0;
    for (int i = 0; i < 8; i++) {
        if (i < uSpawnCount) { total += max(uSpawns[i].z, 1e-3); }
    }
    float pick = h0 * total;
    int chosen = 0;
    float acc = 0.0;
    for (int i = 0; i < 8; i++) {
        if (i < uSpawnCount) {
            acc += max(uSpawns[i].z, 1e-3);
            if (pick <= acc) { chosen = i; break; }
            chosen = i;
        }
    }
    vec4 sp = uSpawns[chosen];
    // Initial jitter is wider than steady-state respawn jitter so the first
    // frame reads as soft clouds around each spawn point, not single dots.
    float jr = sp.w + 0.22;
    float ja = h1 * 6.2831853;
    float jd = sqrt(h2) * jr;
    vec2 p = vec2(sp.x, sp.y) + vec2(cos(ja), sin(ja)) * jd;
    p.x = clamp(p.x, -uAspect, uAspect);
    p.y = clamp(p.y, -1.0, 1.0);
    outPosVel = vec4(p, 0.0, 0.0);
    float ttl = uLife * (0.6 + 0.8 * h3);
    // Stagger age uniformly through the lifetime.
    outMeta = vec4(h2 * ttl, ttl, float(chosen), h3);
}
