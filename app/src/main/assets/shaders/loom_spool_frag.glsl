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

// Loom Spool: a flight through a row of hoops strung to each other with
// straight threads. Each pair of hoops is laced both ways round, every thread
// running from one hoop to a point further round on the next, so the
// straight lines draw a curved waist between them; the two lacings cross,
// one just inside the other. Each spool slowly turns.
//
// Morph turns the hoops against each other: from a straight cage the waist
// pinches deeper. Worst case 90 steps, no noise.

vec4 loomMap(vec3 p) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    float big = 2.2;
    float len = 4.0;
    float k = floor(p.z / len);
    float h = p.z / len - k;
    float twist = mix(0.3, 2.4, gLoomForm);
    // Where this slice cuts the threads: the chord from one hoop to the next, at h, gives their radius and turn.
    vec2 c = vec2(1.0 - h + h * cos(twist), h * sin(twist));
    float rho = big * length(c);
    float turn = loomAngle(c);
    float chord = 2.0 * big * sin(0.5 * twist);
    float slant = len / sqrt(len * len + chord * chord);
    float cell = LOOM_TAU / 24.0;
    float spin = uTime * LOOM_RATE(60) + 0.7 * mod(k, 4.0);
    float d1 = loomRep(th - spin - turn, cell);
    float d2 = loomRep(th - spin + turn, cell);
    vec3 a = loomYarn(vec2(r * cos(d1) - rho + 0.05, r * sin(d1)) * slant, 0.03, 2.0, 6.0 * h, 0.1 * mod(k, 4.0));
    vec3 b = loomYarn(vec2(r * cos(d2) - rho - 0.05, r * sin(d2)) * slant, 0.03, 2.0, 6.0 * h, 0.5);
    vec3 s = loomNearer(a, b);
    float hoop = length(vec2(r - big, loomRep(p.z, len))) - 0.07;
    s = loomNearer(s, vec3(hoop, 4.0 * th / LOOM_TAU, 0.25));
    return vec4(s.x, s.y, 1.0e3, s.z);
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    gLoomStride = 0.6;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 1.1);
}
