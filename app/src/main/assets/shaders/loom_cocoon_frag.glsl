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

// Loom Cocoon: a flight through the inside of a spun cocoon. Three layers of
// silk, each wound at its own slant (steep, shallow and the other way), and
// each thread wandering a little as it goes; behind them a thin parchment
// skin the light comes through.
//
// Morph draws the cocoon in at intervals until the way is a string of pods.
// Worst case 90 steps, no noise.

/** One layer of silk at radius `big`: `count` threads, each `wind` radians round per unit of the way. */
vec3 cocoonSilk(float r, float th, float z, float big, float count, float wind, float phase, float hue) {
    float u = th - wind * z;
    float du = loomRep(u, LOOM_TAU / count);
    float across = r / sqrt(1.0 + r * r * wind * wind);
    float wander = 0.07 * sin(3.0 * th + 0.7853982 * z + phase);
    return loomYarn(vec2(across * du, r - big - wander), 0.03, 2.0, 0.5 * z, hue);
}

vec4 loomMap(vec3 p) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    float pod = 1.75 - 0.65 * gLoomForm * (0.5 + 0.5 * cos(0.7853982 * p.z));
    // Each winding goes whole times round its own spacing in LOOM_PERIOD, so the silk repeats with the world.
    vec3 s = cocoonSilk(r, th, p.z, pod, 5.0, 0.4712389, 0.0, 0.0);
    s = loomNearer(s, cocoonSilk(r, th, p.z, pod + 0.09, 4.0, -0.4908739, 2.1, 0.25));
    s = loomNearer(s, cocoonSilk(r, th, p.z, pod + 0.18, 3.0, 3.1415927, 4.2, 0.55));
    float skin = 0.9 * (pod + 0.45 - r);
    return vec4(s.x * 0.9, s.y, skin, s.z);
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
