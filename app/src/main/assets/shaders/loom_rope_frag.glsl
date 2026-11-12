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

// Loom Rope: a flight down the middle of coiled rope. Two ropes wound round
// the way like a double spring, and two more round them the other way; each
// rope is three strands laid round each other, each strand spun of four
// plies twisted against the lay.
//
// Morph coils the ropes in close about the way and thickens them. Worst case
// 90 steps, no noise.

/** Two ropes half a turn apart, coiled at radius `coil`, a turn every `pitch` units (its sign the hand). */
vec3 ropeCoil(float r, float th, float z, float coil, float pitch, float thick, float hue) {
    float zc = z - pitch * th / LOOM_TAU;
    float apart = 0.5 * abs(pitch);
    float n = floor(zc / apart + 0.5);
    float lap = LOOM_TAU * r;
    // Across the rope: out from the coil, and along the way scaled to the rope's own slant.
    vec2 q = vec2(r - coil, (zc - apart * n) * lap / sqrt(lap * lap + pitch * pitch));
    // Three strands round the rope's core, laid four times round in a lap.
    q = loomRot(4.0 * th) * q;
    float sector = LOOM_TAU / 3.0;
    float a = loomAngle(q);
    float off = loomRep(a, sector);
    vec2 c = length(q) * vec2(cos(off), sin(off)) - vec2(0.15 * thick, 0.0);
    float strand = mod(floor(a / sector + 0.5), 3.0);
    return loomYarn(c, 0.13 * thick, 4.0, -6.0 * th / LOOM_TAU, hue + 0.4 * mod(n, 2.0) + 0.1 * strand);
}

vec4 loomMap(vec3 p) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    float coil = mix(2.0, 1.25, gLoomForm);
    float thick = mix(1.0, 1.5, gLoomForm);
    vec3 s = loomNearer(ropeCoil(r, th, p.z, coil, 4.0, thick, 0.0), ropeCoil(r, th, p.z, coil + 1.4, -4.0, thick, 0.2));
    return vec4(s.x, s.y, 1.0e3, s.z);
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    gLoomStride = 0.65;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 1.0);
}
