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

// Loom Cocoon: a flight through spun silk. Layers of silk, each wound at its
// own slant (steep, shallow and the other way), each thread wandering a little
// as it goes, and behind them a thin parchment skin the light comes through.
//
// Three formulas (Formula, lib_layers), each a different cocoon:
//   0  one cocoon round the way, three layers deep
//   1  a lobed cocoon: four lobes that turn along the way, so the silk
//      bunches into ridges and the skin into a fluted sheath
//   2  a nest: five slim cocoons wound round the way, each its own silk and
//      skin, the way open between them, wound twice as tight; they swell as
//      the iteration grows (Detail, k1, k4)
//
// Morph draws the silk in at intervals until the way is a string of pods.
// Worst case 90 steps, no noise.

/** One layer of silk at radius `big`: `count` threads, each `wind` radians round per unit of the way. */
vec3 cocoonSilk(float r, float th, float z, float big, float count, float wind, float phase, float hue) {
    float u = th - wind * z;
    float du = loomRep(u, LOOM_TAU / count);
    float across = r / sqrt(1.0 + r * r * wind * wind);
    float wander = 0.07 * sin(3.0 * th + 0.7853982 * z + phase);
    return loomYarn(vec2(across * du, r - big - wander), 0.03, 2.0, 0.5 * z, hue);
}

/** A cocoon about a local axis: r, th about it, `pod` its radius; three layers and the skin outside them. */
vec4 cocoonAbout(float r, float th, float z, float pod, float wind, float sheath) {
    // Each winding goes whole times round its own spacing in LOOM_PERIOD, so the silk repeats with the world.
    vec3 s = cocoonSilk(r, th, z, pod, 5.0, 0.4712389 * wind, 0.0, 0.0);
    s = loomNearer(s, cocoonSilk(r, th, z, pod + 0.09, 4.0, -0.4908739 * wind, 2.1, 0.25));
    s = loomNearer(s, cocoonSilk(r, th, z, pod + 0.18, 3.0, 3.1415927, 4.2, 0.55));
    float skin = 0.9 * (pod + sheath - r);
    return vec4(s.x * 0.9, s.y, skin, s.z);
}

float cocoonPod(float z, float big) {
    return big - 0.37 * big * gLoomForm * (0.5 + 0.5 * cos(0.7853982 * z));
}

vec4 cocoonOne(vec3 p) {
    return cocoonAbout(length(p.xy), loomAngle(p.xy), p.z, cocoonPod(p.z, 1.75), 1.0, 0.45);
}

vec4 cocoonLobed(vec3 p) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    float lobe = 0.24 * cos(4.0 * th - 0.7853982 * p.z);
    vec4 m = cocoonAbout(r, th, p.z, cocoonPod(p.z, 1.8) * (1.0 + lobe), 1.0, 0.45);
    // The lobes' slope round the way bounds the step.
    float slope = 1.0 / sqrt(1.0 + pow(1.8 * 0.24 * 4.0 / max(r, 0.8), 2.0));
    return vec4(m.x * slope, m.y, m.z * slope, m.w + 0.1);
}

vec4 cocoonNest(vec3 p) {
    vec2 q = layerSector(p.xy, 5.0) - vec2(2.2, 0.0);
    // Wound twice as tight (whole turns in a period still, so the world repeats); grown with the iteration.
    vec4 m = cocoonAbout(length(q), loomAngle(q), p.z, cocoonPod(p.z + 2.0, mix(0.45, 0.68, gLayerIter)), 2.0, 0.25);
    return vec4(m.xyz, m.w + 0.35);
}

vec4 loomMap(vec3 p) {
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * cocoonOne(p);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * cocoonLobed(p);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * cocoonNest(p);
    return m;
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 1.1);
}
