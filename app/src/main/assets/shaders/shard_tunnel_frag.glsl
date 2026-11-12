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

// Shard Tunnel: a flight down a tunnel lined with plates of glass, nine to a
// ring and a ring every unit of the way, each ring set half a plate round
// from the last like courses of brick. The plates lean in like scales, and
// each has one corner broken off.
//
// Morph breaks the lining loose: the plates turn out of the wall, each its
// own way, and stand further apart. Worst case 100 steps and 3 faces, no
// noise.

vec3 shardMap(vec3 p) {
    float r = length(p.xy);
    vec2 dir = r > 1.0e-4 ? p.xy : vec2(1.0, 0.0);
    float ring = floor(p.z);
    float a = atan(dir.y, dir.x) * 9.0 / SHARD_TAU + 0.5 * mod(ring, 2.0);
    float k = floor(a);
    float h = shardHash(vec2(mod(k, 9.0), mod(ring, 16.0)));
    float al = (a - k - 0.5) * SHARD_TAU / 9.0;
    // The plate's frame: x out from the way, y round it, z along it.
    vec2 t = r * vec2(cos(al), sin(al)) - vec2(2.0, 0.0);
    t = shardRot(gShardForm * 0.9 * (2.0 * h - 1.0)) * t;
    float z = p.z - ring - 0.5;
    float gap = 0.03 + 0.06 * gShardForm;
    vec2 corner = 0.7071068 * vec2(fract(3.1 * h) > 0.5 ? 1.0 : -1.0, fract(5.9 * h) > 0.5 ? 1.0 : -1.0);
    vec2 top = vec2(-1.0e3);
    shardTop(top, abs(t.x - (0.25 + 0.3 * gShardForm) * z) - 0.03);
    shardTop(top, abs(t.y) - 0.64 + gap);
    shardTop(top, abs(z) - 0.5 + gap);
    shardTop(top, dot(vec2(t.y, z), corner) - 0.35 - 0.35 * fract(7.7 * h));
    return vec3(top.x, h, length(top));
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    gShardStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = shardFinish(shardTrace(shardEye(ro, 0.6), rd), 1.0);
}
