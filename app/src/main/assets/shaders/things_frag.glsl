#version 300 es
// Things (core/viz/passes/ThingPass, the state and packing in core/viz/things/ThingPool.hpp): up to 32
// short-lived shapes laid over any style's finished picture. Each is a signed distance to a dot, ring, shard
// or blob, blended toward a second shape while it morphs, softened and dripped downward while it melts. A
// Thing carries no colour of its own: uTint is the current palette at its position, resolved by the renderer.
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;
uniform float uAspect;  // width / height
uniform int uCount;
// Per Thing, three vec4:
//   [0] x, y (uv), size (frame heights), angle
//   [1] shape from, shape to (0 dot, 1 ring, 2 shard, 3 blob), morph 0..1, melt 0..1
//   [2] amount, palette position, seed, unused
uniform vec4 uThing[96];
uniform vec3 uTint[32];

float sdfShape(int shape, vec2 p, float r, float seed) {
    if (shape == 0) return length(p) - r;
    if (shape == 1) return abs(length(p) - 0.8 * r) - 0.2 * r;
    if (shape == 2) {
        vec2 q = abs(p) / r;
        return (q.x * 1.6 + q.y * 0.8 - 1.0) * r * 0.56;
    }
    // Not at the centre, where atan(0, 0) is undefined.
    vec2 q = length(p) > 1e-6 ? p : vec2(1e-6, 0.0);
    float a = atan(q.y, q.x);
    float wob = 0.16 * sin(3.0 * a + seed) + 0.08 * sin(5.0 * a - 2.0 * seed);
    return (length(p) - r * (1.0 + wob)) * 0.8;
}

void main() {
    vec3 col = texture(uScene, vUv).rgb;
    for (int i = 0; i < 32; ++i) {
        if (i >= uCount) break;
        vec4 a = uThing[3 * i];
        vec4 b = uThing[3 * i + 1];
        vec4 c = uThing[3 * i + 2];
        float amount = c.x;
        if (amount <= 0.0) continue;
        float r = a.z;
        float melt = b.w;
        vec2 p = (vUv - a.xy) * vec2(uAspect, 1.0);
        if (length(p) > r * 3.0 + 0.4 * melt) continue;
        // Melting: the shape is read from higher up, so it is drawn lower, and stretched downward.
        p.y += 0.25 * melt * melt;
        p.y /= 1.0 + 1.5 * melt;
        float cs = cos(a.w);
        float sn = sin(a.w);
        p = mat2(cs, -sn, sn, cs) * p;
        int from = int(b.x + 0.5);
        int to = int(b.y + 0.5);
        float d = sdfShape(from, p, r, c.z);
        if (from != to) d = mix(d, sdfShape(to, p, r, c.z), smoothstep(0.0, 1.0, b.z));
        float soft = mix(0.003, 0.5 * r, melt);
        float cover = 1.0 - smoothstep(-soft, soft, d);
        float halo = 0.25 * exp(-max(d, 0.0) / (0.5 * r + 0.002)) * (1.0 - cover);
        float k = amount * (1.0 - 0.5 * melt);
        col = mix(col, uTint[i], clamp(cover * k * 0.9, 0.0, 1.0));
        col += uTint[i] * halo * k;
    }
    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
