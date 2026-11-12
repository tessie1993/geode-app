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

// Shard Wall: a flight down a canyon between two walls of cracked glass. Each
// wall is a mosaic of triangles: a grid of squares whose rows slide their own
// way along, every square split corner to corner one way or the other, and
// every triangle set at its own tilt, so the light catches each one apart.
//
// Morph caves the walls in: the tiles tip further out of true, sink to their
// own depths and stand apart. Worst case 100 steps and 3 faces, no noise.

#define WALL_X 1.7

vec3 shardMap(vec3 p) {
    float side = p.x < 0.0 ? 1.0 : 0.0;
    float row = floor(p.y);
    vec2 u = vec2(p.z + shardHash(vec2(row, 5.0 + side)), p.y);
    vec2 cell = floor(u);
    vec2 f = u - cell;
    float h = shardHash(vec2(mod(cell.x, 16.0), cell.y + 31.0 * side));
    if (h > 0.5) f.x = 1.0 - f.x;
    // Which triangle of the square, its three sides (negative inside) and its middle.
    bool upper = f.y > f.x;
    vec3 sides = upper ? vec3(-f.x, f.y - 1.0, (f.x - f.y) * 0.7071068) : vec3(-f.y, f.x - 1.0, (f.y - f.x) * 0.7071068);
    vec2 mid = upper ? vec2(0.3333333, 0.6666667) : vec2(0.6666667, 0.3333333);
    float id = fract(h * (upper ? 3.7 : 9.1));
    vec2 tilt = (0.16 + 0.8 * gShardForm) * (vec2(id, fract(7.3 * id)) - 0.5);
    float face = WALL_X + 0.35 * gShardForm * fract(5.1 * id) + dot(tilt, f - mid);
    float slab = abs(abs(p.x) - face - 0.04) - 0.04;
    float gap = 0.012 + 0.04 * gShardForm;
    vec2 top = vec2(-1.0e3);
    shardTop(top, slab);
    shardTop(top, sides.x + gap);
    shardTop(top, sides.y + gap);
    shardTop(top, sides.z + gap);
    // A neighbouring tile starts past this one's own crack.
    float inside = -max(sides.x, max(sides.y, sides.z));
    return vec3(min(top.x, inside + gap), id, length(top));
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    gShardStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = shardFinish(shardTrace(shardEye(ro, 0.6), rd), 1.0);
}
