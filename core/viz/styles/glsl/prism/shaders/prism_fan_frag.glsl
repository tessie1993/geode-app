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
//#include lib_prism

// Prism Fan: a flight through a row of chrome fans, one every two units of
// the way. Each is seven thin curved vanes set round an open middle inside a
// chrome hoop, pitched like a turbine's and swept back like a shutter's
// blades; one fan in two is pitched and turned the other way. Lines of light
// run along every vane's edges and round each hoop's rims.
//
// Morph closes the shutter, the vanes reaching in toward the way.
//
// Formula, three turbines (lib_strata): 0 the row of fans, as it always was;
// 0.5 fans on fans: on every vane, out near the hoop, a small fan of five
// vanes turning the other way, and on each of those a smaller one, as many
// as gStrataIters past the first, the last growing in; 1 a screw: the fans
// run together along the way into one seven-bladed helicoid round the open
// middle, a quarter turn to every unit (whole turns in 16, so the period
// holds), flown down its throat. The far fractal follows: an Apollonian ball,
// a kaleidoscope dust, a Menger cube.
//
// Worst case 108 steps, at most three folds a step, no noise; then the
// layers.

#define FAN_N 7.0

/** A small fan of five vanes at v about its own hub, radius `rad`: its distance in the plane. */
float fanlet(vec2 v, float rad, float spin) {
    vec2 q = prismFold(prismRot(spin) * v, 5.0);
    float r = length(v);
    return max(q.y - 0.02 * rad, max(0.15 * rad - r, r - rad));
}

/** The row of fans at p, fans on its vanes `nest` deep, or one screw when `screw`: x the distance, y the ribbon's. */
vec2 turbine(vec3 p, float nest, bool screw) {
    float cell = floor(p.z * 0.5 + 0.5);
    float zl = screw ? 0.0 : p.z - 2.0 * cell;
    float side = screw ? 1.0 : mod(cell, 2.0) * 2.0 - 1.0;
    float r = length(p.xy);
    float pitch = screw ? 1.5707963 : 1.4;
    float sweep = 0.5;
    vec2 v = prismRot(side * (pitch * (screw ? p.z : zl) + uTime * PRISM_RATE(45)) + sweep * r) * p.xy;
    vec2 q = prismFold(v, FAN_N);
    // The vanes' twist and sweep stretch the fold, so their distance is scaled back to a bound.
    float stretch = sqrt(1.0 + r * r * (pitch * pitch + sweep * sweep));
    float vane = (q.y - 0.03) / stretch;
    float open = mix(1.25, 0.55, gPrismForm);
    float d = max(vane, max(screw ? -1.0e3 : abs(zl) - 0.35, max(open - r, r - 2.5)));
    float seam = screw ? abs(r - open) : min(abs(r - open), abs(abs(zl) - 0.35));
    // Fans on the vanes: each in the fan's plane on a vane, its hub out near the hoop, turning the other way.
    float rad = 0.7;
    vec2 hub = vec2(1.85, 0.0);
    for (int i = 0; i < 3; i++) {
        float f = clamp(nest - float(i), 0.0, 1.0);
        if (f <= 0.0) break;
        vec2 l = q - hub;
        float small = fanlet(l, rad * f, -side * uTime * PRISM_RATE(160 + 80 * i) + 0.6 * float(i));
        float fl = max(small / stretch, abs(zl) - 0.03 * f);
        seam = fl < d ? min(seam, length(l)) : seam;
        d = min(d, fl);
        // The next one on this one's first vane, out near its rim.
        q = prismFold(l, 5.0);
        hub = vec2(0.62 * rad * f, 0.0);
        rad *= 0.38;
    }
    if (!screw) {
        float hoop = length(vec2(r - 2.56, zl)) - 0.07;
        seam = d < hoop ? seam : abs(zl);
        d = min(d, hoop);
        // Each fan keeps within 0.45 of its middle, and the next one turns another way.
        d = min(d, 1.55 - abs(zl));
    }
    return vec2(d, seam);
}

vec2 prismMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = turbine(p, 0.0, false);
    if (strataUses(1)) b = turbine(p, gStrataIters, false);
    if (strataUses(2)) c = turbine(p, 0.0, true);
    return strataBlend2(a, b, c);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    strataSetup(vec3(2.0, 0.0, 3.0), 0.0, 0.45, 0.7);
    gPrismStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.2);
}
