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
// Three formulas (Formula, lib_layers), each a different spool:
//   0  round hoops, one waist to a spool
//   1  square frames: the lacing runs between square hoops, so every waist
//      pinches to a rounded square and the corners flare
//   2  nested spools: a second, finer spool laced inside the first, turning
//      the other way, its waist pinched deeper; the pair reads as two
//      hourglasses one inside the other
//
// Morph turns the hoops against each other: from a straight cage the waist
// pinches deeper. Worst case 90 steps, no noise.

/** One spool's lacing at radius r, angle th along the way: hoops `big` across, `twist` the turn from hoop to hoop. */
vec3 spoolLace(float r, float th, float z, float big, float twist, float threads, float dir, float hue) {
    float len = 4.0;
    float k = floor(z / len);
    float h = z / len - k;
    // Where this slice cuts the threads: the chord from one hoop to the next, at h, gives their radius and turn.
    vec2 c = vec2(1.0 - h + h * cos(twist), h * sin(twist));
    float rho = big * length(c);
    float turn = loomAngle(c);
    float chord = 2.0 * big * sin(0.5 * twist);
    float slant = len / sqrt(len * len + chord * chord);
    float cell = LOOM_TAU / threads;
    float spin = dir * uTime * LOOM_RATE(60) + 0.7 * mod(k, 4.0);
    float d1 = loomRep(th - spin - turn, cell);
    float d2 = loomRep(th - spin + turn, cell);
    vec3 a = loomYarn(vec2(r * cos(d1) - rho + 0.05, r * sin(d1)) * slant, 0.03, 2.0, 6.0 * h, hue + 0.1 * mod(k, 4.0));
    vec3 b = loomYarn(vec2(r * cos(d2) - rho - 0.05, r * sin(d2)) * slant, 0.03, 2.0, 6.0 * h, hue + 0.5);
    return loomNearer(a, b);
}

vec4 spoolRound(vec3 p, float twist) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    vec3 s = spoolLace(r, th, p.z, 2.2, twist, 24.0, 1.0, 0.0);
    float hoop = length(vec2(r - 2.2, loomRep(p.z, 4.0))) - 0.07;
    s = loomNearer(s, vec3(hoop, 4.0 * th / LOOM_TAU, 0.25));
    return vec4(s.x, s.y, 8.0, s.z);
}

vec4 spoolSquare(vec3 p, float twist) {
    float th = loomAngle(p.xy);
    // A square's radius in the plane: laced as if round in it, the square's slope bounds the step.
    float r = max(abs(p.x), abs(p.y)) * 1.12;
    vec3 s = spoolLace(r, th, p.z, 2.2, twist, 24.0, 1.0, 0.2);
    float hoop = length(vec2(r - 2.2, loomRep(p.z, 4.0))) - 0.08;
    s = loomNearer(s, vec3(hoop, 4.0 * th / LOOM_TAU, 0.35));
    return vec4(s.x * 0.7, s.y, 8.0, s.z);
}

vec4 spoolNested(vec3 p, float twist) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    vec3 s = spoolLace(r, th, p.z, 2.6, twist, 24.0, 1.0, 0.1);
    // The inner spool, turning the other way and pinched deeper; it thickens with the iteration.
    s = loomNearer(s, spoolLace(r, th, p.z + 2.0, 1.6, min(twist + 0.9, 2.9), 16.0, -1.0, 0.55));
    float hoop = min(length(vec2(r - 2.6, loomRep(p.z, 4.0))) - 0.07,
        length(vec2(r - 1.6, loomRep(p.z + 2.0, 4.0))) - mix(0.04, 0.09, gLayerIter));
    s = loomNearer(s, vec3(hoop, 4.0 * th / LOOM_TAU, 0.8));
    return vec4(s.x, s.y, 8.0, s.z);
}

vec4 loomMap(vec3 p) {
    float twist = mix(0.3, 2.4, gLoomForm);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * spoolRound(p, twist);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * spoolSquare(p, twist);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * spoolNested(p, twist);
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
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 1.1);
}
