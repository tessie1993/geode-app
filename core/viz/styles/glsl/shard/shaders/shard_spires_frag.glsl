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

// Shard Spires: a flight low over a field of crystal spires, one to a cell,
// each a six-sided column ending in a six-faced point, turned and leaning its
// own way and rising out of the dark below.
//
// Morph grows them from stubs to tall points that reach past the eye.
//
// Formula, three crystal grounds (lib_strata): 0 the field of spires, as it
// always was; 0.5 a druse, a second field half the size grown among the
// first and a third a quarter the size among those, as many as
// gStrataIters, the last growing in; 1 a geode, the field mirrored into a
// roof of spires hanging point down, set half a cell over, the eye flying
// between the two. The far fractal follows: a kaleidoscope dust, an
// Apollonian ball, a Menger cube.
//
// Worst case 100 steps and 3 faces, at most three fields a step, no noise;
// then the layers.

#define SPIRE_BASE -2.4

/** The field of spires at p, their tips `grow` of Morph's height (0 a field of stubs at the base). */
vec3 spireField(vec3 p, float grow) {
    vec2 c = floor(p.xz * 0.5);
    float h = shardHash(c - vec2(0.0, 8.0) * floor(c.y / 8.0));
    vec2 o = p.xz - 2.0 * c - 1.0;
    // Every spire keeps 0.2 inside its own cell, so a neighbour's is never nearer than this one's wall.
    float wall = 1.0 - max(abs(o.x), abs(o.y)) + 0.2;
    vec2 lean = 0.2 * (vec2(fract(2.3 * h), fract(4.7 * h)) - 0.5);
    vec2 xz = o - 0.3 * (vec2(h, fract(9.7 * h)) - 0.5) - lean * (p.y - SPIRE_BASE);
    xz = shardRot(SHARD_TAU * fract(1.9 * h)) * xz;
    float w = 0.16 + 0.12 * fract(6.1 * h);
    float top = SPIRE_BASE + grow * (1.1 + mix(0.45, 1.9, gShardForm) * (0.45 + 0.55 * fract(3.9 * h)));
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

/** The field at `scale` (1 the first), its cells moved `shift` cells over; distances back in world units. */
vec3 spireScaled(vec3 p, float scale, float shift, float grow) {
    // Each smaller field stands on the same ground.
    vec3 q = vec3(p.x / scale + shift, (p.y - SPIRE_BASE) / scale + SPIRE_BASE, p.z / scale + shift);
    vec3 m = spireField(q, grow);
    return vec3(m.x * scale, m.y, m.z * scale);
}

vec3 shardMap(vec3 p) {
    vec3 a = vec3(1.0e3);
    vec3 b = a;
    vec3 c = a;
    if (strataUses(0) || strataUses(1)) a = spireField(p, 1.0);
    if (strataUses(1)) {
        // The druse: fields at a half and a quarter, the last of gStrataIters growing in.
        b = a;
        for (int i = 1; i < 3; i++) {
            float g = clamp(gStrataIters - float(i) + 1.0, 0.0, 1.0);
            if (g <= 0.0) break;
            vec3 m = spireScaled(p, exp2(-float(i)), 0.5, g);
            b = m.x < b.x ? m : b;
        }
    }
    if (strataUses(2)) {
        // The geode: above the eye the field hangs from a roof, half a cell over.
        vec3 q = p;
        if (q.y > 0.0) q = vec3(q.x + 1.0, -q.y, q.z + 1.0);
        c = spireField(q, 0.6);
        // The other half's tips stop 0.6 short of the middle, so it is never nearer than this.
        c.x = min(c.x, abs(p.y) + 0.6);
    }
    return strataBlend(a, b, c);
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    strataSetup(vec3(0.0, 2.0, 3.0), 0.0, 0.55, 0.8);
    gShardStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, SHARD_PERIOD);
    fragColor = shardFinish(shardTrace(ro, rd), 1.1);
}
