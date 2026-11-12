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

// Shard Rods: a flight through a scaffold of three-sided glass rods, one
// along each of the three ways through every cell of a lattice, so the long
// edges draw a frame of light in the dark. Every rod is snapped at both ends
// of its cell on a slant, and turns slowly on its own length.
//
// Morph thickens the rods from fine threads to heavy beams.
//
// Formula, three scaffolds (lib_strata): 0 the straight rods, as they always
// were; 0.5 the scaffold cut by a Menger sponge eight units across, so it
// breaks into a cathedral of square vaults at every scale round the open
// way (the sponge's middle tunnel); 1 molten glass: every rod twisted on its
// length and the three joined by a smooth union, so each joint is a swelling
// knot. The far fractal follows: a Menger cube, a kaleidoscope dust, a
// Mandelbox.
//
// Worst case 100 steps and 3 faces, three rods and at most 3 sponge folds a
// step, no noise; then the layers.

/**
 * One rod: c across it, x along it from the cell's middle, phi its turn. A
 * triangle of inradius `rad` across, snapped short of the cell's ends on a
 * slant `h` picks. x the distance, y the edge's.
 */
vec2 rod(vec2 c, float x, float phi, float rad, float h) {
    vec2 n0 = vec2(cos(phi), sin(phi));
    vec2 n1 = vec2(-0.5 * n0.x - 0.8660254 * n0.y, 0.8660254 * n0.x - 0.5 * n0.y);
    vec2 n2 = vec2(-0.5 * n0.x + 0.8660254 * n0.y, -0.8660254 * n0.x - 0.5 * n0.y);
    vec2 top = vec2(-1.0e3);
    shardTop(top, dot(c, n0) - rad);
    shardTop(top, dot(c, n1) - rad);
    shardTop(top, dot(c, n2) - rad);
    shardTop(top, abs(x) + 0.3 * dot(c, n0) * (2.0 * h - 1.0) - 0.8);
    return vec2(top.x, length(top));
}

/** The scaffold at p, each rod twisted `twist` radians a unit along its length (a bound still: under 1.1 times the true distance at the thickest), joined by a smooth union `melt` wide (0: plain min). */
vec3 scaffold(vec3 p, float twist, float melt) {
    vec3 cell = floor(p * 0.5);
    vec3 q = p - 2.0 * cell - 1.0;
    cell.z = mod(cell.z, 8.0);
    float rad = mix(0.05, 0.22, gShardForm);
    float hx = shardHash(cell.yz + 0.5);
    float hy = shardHash(cell.zx + 3.5);
    float hz = shardHash(cell.xy + 7.5);
    vec2 a = rod(q.yz, q.x, twist * q.x + SHARD_TAU * hx + uTime * SHARD_RATE(60.0 + 60.0 * floor(3.0 * hx)), rad, fract(5.3 * hx));
    vec2 b = rod(q.zx, q.y, twist * q.y + SHARD_TAU * hy - uTime * SHARD_RATE(60.0 + 60.0 * floor(3.0 * hy)), rad, fract(5.3 * hy));
    vec2 c = rod(q.xy, q.z, twist * q.z + SHARD_TAU * hz + uTime * SHARD_RATE(40.0 + 40.0 * floor(3.0 * hz)), rad, fract(5.3 * hz));
    vec3 m = a.x < b.x ? vec3(a.x, hx, a.y) : vec3(b.x, hy, b.y);
    m = c.x < m.x ? vec3(c.x, hz, c.y) : m;
    if (melt > 0.0) {
        // The smooth union of all three: the joints swell, the faces keep their light.
        float k = melt;
        float u = min(a.x, b.x) - 0.25 * max(k - abs(a.x - b.x), 0.0) * max(k - abs(a.x - b.x), 0.0) / k;
        u = min(u, c.x) - 0.25 * max(k - abs(u - c.x), 0.0) * max(k - abs(u - c.x), 0.0) / k;
        m.x = u;
    }
    return m;
}

vec3 shardMap(vec3 p) {
    vec3 a = vec3(1.0e3);
    vec3 b = a;
    vec3 c = a;
    if (strataUses(0) || strataUses(1)) a = scaffold(p, 0.0, 0.0);
    if (strataUses(1)) {
        // One sponge to every eight units, along the way too (dividing the period); its middle tunnel is the way.
        vec3 q = p - 8.0 * floor(p / 8.0 + 0.5);
        float sponge = strataSponge(q, 4.0, gStrataIters);
        b = vec3(max(a.x, sponge), a.y, min(a.z, length(vec2(a.x, sponge))));
    }
    if (strataUses(2)) c = scaffold(p, 1.5, 0.12 + 0.2 * gShardForm);
    return strataBlend(a, b, c);
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    strataSetup(vec3(3.0, 0.0, 1.0), 1.0, 0.5, 0.7);
    gShardStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, SHARD_PERIOD);
    fragColor = shardFinish(shardTrace(ro, rd), 1.0);
}
