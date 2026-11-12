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

// Shard Tunnel: a flight down a tunnel lined with plates of glass, nine to a
// ring and a ring every unit of the way, each ring set half a plate round
// from the last like courses of brick. The plates lean in like scales, and
// each has one corner broken off.
//
// Morph breaks the lining loose: the plates turn out of the wall, each its
// own way, and stand further apart.
//
// Formula, three linings (lib_strata): 0 the rings of plates, as they always
// were; 0.5 every plate pierced by a Menger sponge in its own plane, square
// holes within square holes, gStrataIters folds; 1 a screw: the rings run
// into one helix, a turn to every unit of the way, and the plates stand open
// like louvres, so the far layer shows between them. The far fractal follows:
// a kaleidoscope dust, a Mandelbox, a Menger cube.
//
// Worst case 100 steps and 3 faces, one plate and at most 3 sponge folds a
// step, no noise; then the layers.

/** The lining at p: rings of plates, or one helix of louvres when `screw`. */
vec3 lining(vec3 p, bool screw, bool pierce) {
    float r = length(p.xy);
    vec2 dir = r > 1.0e-4 ? p.xy : vec2(1.0, 0.0);
    float turn = atan(dir.y, dir.x) / SHARD_TAU;
    // The screw climbs a whole ring a turn; its seam at the back falls on a plate's edge.
    float u = screw ? p.z + turn : p.z;
    float ring = floor(u);
    float a = turn * 9.0 + (screw ? 0.5 : 0.5 * mod(ring, 2.0));
    float k = floor(a);
    float h = shardHash(vec2(mod(k, 9.0), mod(ring, 16.0)));
    float al = (a - k - 0.5) * SHARD_TAU / 9.0;
    // The plate's frame: x out from the way, y round it, z along it.
    vec2 t = r * vec2(cos(al), sin(al)) - vec2(2.0, 0.0);
    t = shardRot(gShardForm * 0.9 * (2.0 * h - 1.0)) * t;
    float z = u - ring - 0.5;
    if (screw) {
        // A louvre: the plate turned on its own width, open to the way.
        vec2 tz = shardRot(0.7) * vec2(t.x, z);
        t.x = tz.x;
        z = tz.y;
    }
    float gap = 0.03 + 0.06 * gShardForm;
    vec2 corner = 0.7071068 * vec2(fract(3.1 * h) > 0.5 ? 1.0 : -1.0, fract(5.9 * h) > 0.5 ? 1.0 : -1.0);
    vec2 top = vec2(-1.0e3);
    shardTop(top, abs(t.x - (0.25 + 0.3 * gShardForm) * z) - 0.03);
    shardTop(top, abs(t.y) - 0.64 + gap);
    shardTop(top, abs(z) - 0.5 + gap);
    shardTop(top, dot(vec2(t.y, z), corner) - 0.35 - 0.35 * fract(7.7 * h));
    float edge = length(top);
    if (pierce) {
        // The plate lies in the sponge's top layer, where its cut is a carpet.
        float sponge = strataSponge(vec3(t.y, z, t.x + 0.43), 0.64, gStrataIters);
        edge = min(edge, length(vec2(top.x, sponge)));
        top.x = max(top.x, sponge);
    }
    return vec3(top.x, h, edge);
}

vec3 shardMap(vec3 p) {
    vec3 a = vec3(1.0e3);
    vec3 b = a;
    vec3 c = a;
    if (strataUses(0)) a = lining(p, false, false);
    if (strataUses(1)) b = lining(p, false, true);
    if (strataUses(2)) c = lining(p, true, false);
    return strataBlend(a, b, c);
}

//#include lib_shard_march

void main() {
    vec2 uv = view();
    shardSetup();
    strataSetup(vec3(0.0, 1.0, 3.0), 1.0, 0.5, 0.8);
    gShardStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = shardFinish(shardTrace(shardEye(ro, 0.6), rd), 1.0);
}
