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

// Loom Braid: the family's opener. A flight down the inside of a braided
// cable: two sleeves of cords, one inside the other, each wound one way round
// the way ahead and the other way back, every cord laid over the next one it
// crosses and under the one after, and spun of three plies.
//
// Morph pulls the braid tight: the cords swell until they press together and
// the over and under deepens. Worst case 90 steps, no noise.

/** One braided sleeve of radius `big`, `cords` cords each way: once round in every LOOM_PERIOD. */
vec3 braidSleeve(float r, float th, float z, float big, float cords, float rad, float hue) {
    float wind = LOOM_TAU / LOOM_PERIOD;
    float cell = LOOM_TAU / cords;
    // Where a cord of one hand crosses one of the other, cos(cords * th) is +1 or -1 in turn: over, then under.
    float lift = (rad + 0.02) * cos(cords * th);
    float across = r / sqrt(1.0 + r * r * wind * wind);
    float u = th - wind * z;
    float v = th + wind * z;
    float iu = floor(u / cell + 0.5);
    float iv = floor(v / cell + 0.5);
    vec3 a = loomYarn(vec2(across * (u - cell * iu), r - big - lift), rad, 3.0, 0.5 * z, hue + 0.12 * mod(iu, 3.0));
    vec3 b = loomYarn(vec2(across * (v - cell * iv), r - big + lift), rad, 3.0, -0.5 * z, hue + 0.5 + 0.12 * mod(iv, 3.0));
    return loomNearer(a, b);
}

vec4 loomMap(vec3 p) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    float rad = mix(0.07, 0.19, gLoomForm);
    vec3 s = loomNearer(braidSleeve(r, th, p.z, 1.5, 12.0, rad, 0.0), braidSleeve(r, th, p.z, 2.9, 18.0, 1.3 * rad, 0.25));
    return vec4(s.x, s.y, 1.0e3, s.z);
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 1.0);
}
