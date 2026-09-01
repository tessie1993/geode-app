#version 300 es
// Rebuilt particle update kernel - the spawn -> flow -> catch -> respawn
// lifecycle:
//   1. inertia through the velocity field: v += (flow - v) * k (frame-rate
//      independent, uDrag is the per-1/60s factor),
//   2. catch-point attraction: inverse-square pull with softening epsilon,
//      magnitude soft-capped so close passes slingshot instead of exploding,
//   3. capture: entering a catch radius recycles the particle at a CURRENT
//      spawn point - as the choreography progresses, the population
//      physically migrates with it,
//   4. lifetime: age past ttl also recycles (staggered ttls, no waves).
// MRT: state A = (pos.xy, vel.xy), state B = (age, ttl, emitterIndex, seed).
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float velocity values far exceed that; on GPUs
// honoring sampler precision (Mali) every read clamps and quantizes.
precision highp sampler2D;
in vec2 vUv;
uniform highp sampler2D uState;          // A: xy pos (sim), zw vel (sim/s)
uniform highp sampler2D uMeta;           // B: age, ttl, emitterIndex, seed
uniform highp sampler2D uVelocityField;  // fluid/curl velocity grid
uniform highp float uAspect;
uniform float uDt;
uniform float uDrag;
uniform float uFlowScale;      // grid velocity -> sim units per second
uniform float uTime;
uniform float uLife;           // base lifetime seconds
uniform vec4 uSpawns[8];       // (x, y, weight, jitterRadius)
uniform int uSpawnCount;
uniform vec4 uCatches[4];      // (x, y, pull, captureRadius)
uniform int uCatchCount;
layout(location = 0) out vec4 outPosVel;
layout(location = 1) out vec4 outMeta;
float hash(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract((p.x + p.y) * p.x);
}
void respawn(float seedShift) {
    float h0 = hash(vUv * 1.37 + uTime + seedShift);
    float h1 = hash(vUv * 2.11 + uTime * 1.61 + seedShift);
    float h2 = hash(vUv * 3.71 + uTime * 0.73 + seedShift);
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
    float ja = h1 * 6.2831853;
    float jd = sqrt(h2) * max(sp.w, 1e-3);
    vec2 p = vec2(sp.x, sp.y) + vec2(cos(ja), sin(ja)) * jd;
    p.x = clamp(p.x, -uAspect, uAspect);
    p.y = clamp(p.y, -1.0, 1.0);
    outPosVel = vec4(p, 0.0, 0.0);
    outMeta = vec4(0.0, uLife * (0.6 + 0.8 * h2), float(chosen), h2);
}
void main() {
    vec4 s = texture(uState, vUv);
    vec4 m = texture(uMeta, vUv);
    vec2 p = s.xy;
    vec2 v = s.zw;
    float age = m.x + uDt;
    float ttl = max(m.y, 1e-3);

    // Lifetime recycle at the CURRENT spawn choreography.
    if (age > ttl) { respawn(3.7); return; }

    // Catch points: attraction + capture. A zero-pull well is fully inert
    // (no invisible capture), and newborn particles get a grace period so a
    // well overlapping a spawn point can't recycle births before they even
    // fade in.
    for (int i = 0; i < 4; i++) {
        if (i >= uCatchCount) { break; }
        vec4 c = uCatches[i];
        if (c.z <= 0.0) { continue; }
        vec2 d = vec2(c.x, c.y) - p;
        float dist2 = dot(d, d);
        if (age > 0.5 && dist2 < c.w * c.w) { respawn(9.1 + float(i)); return; }
        // Inverse-square pull, softened + soft-capped (no slingshots).
        float f = c.z / (dist2 + 0.05);
        f = f * 6.0 / (6.0 + f);
        // Not normalize(d): a newborn (age <= 0.5, capture-immune) particle
        // sitting exactly on the catch center would divide by zero and latch
        // NaN into the state texture (this kernel has no NaN scrub).
        v += d * inversesqrt(max(dist2, 1e-6)) * f * uDt;
    }

    // sim -> texel space for the field fetch.
    vec2 uv = vec2(p.x / uAspect, p.y) * 0.5 + 0.5;
    vec2 flow = texture(uVelocityField, clamp(uv, 0.0, 1.0)).xy * uFlowScale;
    // Frame-rate-independent inertia: uDrag is the per-1/60s blend factor.
    float k = 1.0 - pow(1.0 - uDrag, uDt * 60.0);
    v += (flow - v) * k;
    p += v * uDt;
    // Wrap at the domain edges so the field never empties.
    if (p.x >  uAspect) { p.x -= 2.0 * uAspect; }
    if (p.x < -uAspect) { p.x += 2.0 * uAspect; }
    if (p.y >  1.0) { p.y -= 2.0; }
    if (p.y < -1.0) { p.y += 2.0; }
    outPosVel = vec4(p, v);
    outMeta = vec4(age, ttl, m.z, m.w);
}
