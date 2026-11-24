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

// Shard Wall: a flight down a canyon between two walls of cracked glass. Each
// wall is a mosaic of triangles: a grid of squares whose rows slide their own
// way along, every square split corner to corner one way or the other, and
// every triangle set at its own tilt, so the light catches each one apart.
//
// Morph caves the walls in: the tiles tip further out of true, sink to their
// own depths and stand apart.
//
// Formula, three walls (lib_strata): 0 the two walls of triangles, as they
// always were; 0.5 every square of the mosaic left whole and cut as a Menger
// carpet, squares within squares, gStrataIters folds; 1 a square shaft: the
// walls mirrored onto a floor and a roof, so tiles close in on all four
// sides. The far fractal follows: an Apollonian ball, a Menger cube, a
// kaleidoscope dust.
//
// Worst case 100 steps and 3 faces, one tile and at most 3 sponge folds a
// step, no noise; then the layers.

#define WALL_X 1.7

/** The mosaic at p: triangles, or whole squares cut as a Menger carpet when `carpet`. */
vec3 mosaic(vec3 p, bool carpet) {
    float side = p.x < 0.0 ? 1.0 : 0.0;
    float row = floor(p.y);
    vec2 u = vec2(p.z + shardHash(vec2(row, 5.0 + side)), p.y);
    vec2 cell = floor(u);
    vec2 f = u - cell;
    float h = shardHash(vec2(mod(cell.x, 16.0), cell.y + 31.0 * side));
    if (h > 0.5) f.x = 1.0 - f.x;
    // Which triangle of the square, its three sides (negative inside) and its middle.
    bool upper = f.y > f.x && !carpet;
    vec3 sides = upper ? vec3(-f.x, f.y - 1.0, (f.x - f.y) * 0.7071068) : vec3(-f.y, f.x - 1.0, (f.y - f.x) * 0.7071068);
    vec2 mid = upper ? vec2(0.3333333, 0.6666667) : vec2(0.6666667, 0.3333333);
    if (carpet) {
        // The whole square: its four sides, about its own middle.
        sides = vec3(abs(f.x - 0.5) - 0.5, abs(f.y - 0.5) - 0.5, -1.0);
        mid = vec2(0.5);
    }
    float id = fract(h * (upper ? 3.7 : 9.1));
    vec2 tilt = (0.16 + 0.8 * gShardForm) * (vec2(id, fract(7.3 * id)) - 0.5);
    float face = WALL_X + 0.35 * gShardForm * fract(5.1 * id) + dot(tilt, f - mid);
    float across = abs(p.x) - face - 0.04;
    float slab = abs(across) - 0.04;
    float gap = 0.012 + 0.04 * gShardForm;
    vec2 top = vec2(-1.0e3);
    shardTop(top, slab);
    shardTop(top, sides.x + gap);
    shardTop(top, sides.y + gap);
    shardTop(top, sides.z + gap);
    float edge = length(top);
    if (carpet) {
        // The tile lies in the sponge's top layer, where its cut is a carpet.
        float sponge = strataSponge(vec3(f - 0.5, across + 0.33), 0.5, gStrataIters);
        edge = min(edge, length(vec2(top.x, sponge)));
        top.x = max(top.x, sponge);
    }
    // A neighbouring tile starts past this one's own crack.
    float inside = -max(sides.x, max(sides.y, sides.z));
    return vec3(min(top.x, inside + gap), id, edge);
}

vec3 shardMap(vec3 p) {
    vec3 a = vec3(1.0e3);
    vec3 b = a;
    vec3 c = a;
    if (strataUses(0)) a = mosaic(p, false);
    if (strataUses(1)) b = mosaic(p, true);
    if (strataUses(2)) {
        // The shaft: whichever pair of walls p is nearer, the floor and roof laid like the walls.
        vec3 q = abs(p.y) > abs(p.x) ? vec3(p.y, -p.x, p.z + 0.5) : p;
        c = mosaic(q, false);
    }
    return strataBlend(a, b, c);
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    strataSetup(vec3(2.0, 3.0, 0.0), 2.0, 0.5, 0.8);
    gShardStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = shardFinish(shardTrace(shardEye(ro, 0.6), rd), 1.0);
}
