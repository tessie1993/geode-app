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
//#include lib_loom

// Loom Cloth: a flight through open cloth, each a plain weave: warp threads
// along the way, weft across it, each thread over one and under the next,
// warp and weft in their own colours.
//
// Three formulas (Formula, lib_layers), each a different drape:
//   0  flat sheets stacked above and below the way, each set a little aside
//      from the last
//   1  the cloth rolled into tubes: nested sleeves of weave round the way,
//      the weft running round each one
//   2  the sheets pleated: folded in a zigzag, with finer pleats folded into
//      the large ones as the iteration grows (Detail, k1, k4)
//
// Morph lifts the cloth into slow billows that run along the way. Worst case
// 90 steps, no noise.

/** A plain weave in a sheet's own coordinates: x across, y out of it, z along; `k` the sheet's number. */
vec3 clothWeave(float x, float y, float z, float k) {
    float w = 0.4;
    float rad = 0.075;
    float lift = 0.07;
    float i = floor(x / w + 0.5);
    float j = floor(z / w + 0.5);
    float si = 1.0 - 2.0 * mod(i, 2.0);
    float sj = 1.0 - 2.0 * mod(j, 2.0);
    // Where warp i meets weft j, cos(PI x / w) is si and cos(PI z / w) is sj: the one is up where the other is down.
    vec3 warp = loomYarn(vec2(x - w * i, y - lift * si * cos(LOOM_PI * z / w)), rad, 3.0, 2.0 * z, 0.13 * k);
    vec3 weft = loomYarn(vec2(z - w * j, y + lift * sj * cos(LOOM_PI * x / w)), rad, 3.0, 2.0 * x, 0.4 + 0.13 * k);
    return loomNearer(warp, weft);
}

vec4 clothSheets(vec3 p) {
    float gap = 2.0;
    float k = floor((p.y - 1.0) / gap + 0.5);
    float y = p.y - 1.0 - gap * k;
    float x = p.x + 0.2 * k;
    y -= 0.25 * gLoomForm * sin(0.6 * x + 0.7853982 * p.z + 1.7 * k);
    vec3 s = clothWeave(x, y, p.z, k);
    return vec4(s.x, s.y, 8.0, s.z);
}

vec4 clothTubes(vec3 p) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    // Sleeves 1.5 apart from 1.4 out, each with a whole number of warp threads round it.
    float k = max(floor((r - 1.4) / 1.5 + 0.5), 0.0);
    float big = 1.4 + 1.5 * k + 0.2 * gLoomForm * sin(3.0 * th + 0.7853982 * p.z + k);
    float count = 2.0 * floor(LOOM_TAU * (1.4 + 1.5 * k) / 0.8 + 0.5);
    float x = th * count * 0.4 / LOOM_TAU;
    // Across a thread in world units, out of the sleeve, along the way.
    vec3 s = clothWeave(x, r - big, p.z, k);
    float stretch = r * LOOM_TAU / (count * 0.4);
    s.x *= min(stretch, 1.0);
    return vec4(s.x * 0.85, s.y, 8.0, s.z);
}

/** A triangle wave of x with period 2 h, -1..1. */
float clothZig(float x, float h) {
    return 2.0 * abs(x / h - 2.0 * floor(0.5 * x / h + 0.5)) - 1.0;
}

vec4 clothPleats(vec3 p) {
    float gap = 2.4;
    float k = floor((p.y - 1.2) / gap + 0.5);
    float y = p.y - 1.2 - gap * k;
    float x = p.x + 0.3 * k;
    // The large pleats, and the fine ones folded into them as the iteration grows.
    float fine = smoothstep(0.2, 0.9, gLayerIter);
    float fold = 0.32 * clothZig(x, 1.3) + 0.09 * fine * clothZig(x + 0.2, 0.33);
    y -= fold + 0.25 * gLoomForm * sin(0.6 * x + 0.7853982 * p.z + 1.7 * k);
    vec3 s = clothWeave(x, y, p.z, k + 2.0);
    // Their slopes (0.5 and 0.55) bound the step.
    return vec4(s.x * 0.6, s.y, 8.0, s.z);
}

vec4 loomMap(vec3 p) {
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * clothSheets(p);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * clothTubes(p);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * clothPleats(p);
    return m;
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    gLoomStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 0.9);
}
