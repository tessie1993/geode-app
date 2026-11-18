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

// Prism Nest: three chrome spheres one inside the next, turned about. Each
// thin shell is pierced by fourteen round windows, a ring of six above its
// waist, six below and one at each pole, and each shell turns on its own
// axis, so the windows of the inner shells drift past those of the outer ones
// and every shell mirrors its neighbours. Lines of light ring every window and
// each shell's waist.
//
// Morph opens the windows until each shell is more lattice than wall.
//
// Formula, three nests (lib_strata): 0 the three shells, as they always were;
// 0.5 the shells pierced as well by one still Menger sponge, so their windows
// slide over square holes within square holes as they turn, gStrataIters
// folds; 1 shells within shells: one shell repeated inward, each two thirds
// of the last and turned its own way, as many as gStrataIters past the
// third, the innermost growing in. The far fractal follows: a Menger cube, a
// Mandelbox, an Apollonian ball.
//
// Worst case 108 steps, three shells and at most 3 sponge folds, or one
// nested shell, a step, no noise; then the layers.

float nestShell(vec3 p, float rad, float hole, out float seam) {
    vec2 q = prismFold(p.xz, 6.0);
    vec3 f = vec3(q.x, abs(p.y), q.y);
    // One window per mirror, 35 degrees above or below the waist, and one at each pole.
    float side = length(f - rad * vec3(0.819, 0.574, 0.0)) - hole;
    float pole = length(f - vec3(0.0, rad, 0.0)) - 1.2 * hole;
    float win = min(side, pole);
    seam = min(abs(win), abs(p.y));
    return max(abs(length(p) - rad) - 0.035, -win);
}

/** The three shells at p: x the distance, y the ribbon's. */
vec2 shells(vec3 p) {
    float open = mix(0.18, 0.36, gPrismForm);
    vec3 a = p;
    a.xz = prismRot(uTime * PRISM_RATE(50)) * a.xz;
    vec3 b = p;
    b.yz = prismRot(0.6 - uTime * PRISM_RATE(65)) * b.yz;
    vec3 c = p;
    c.xy = prismRot(1.1 + uTime * PRISM_RATE(100)) * c.xy;
    float s0;
    float s1;
    float s2;
    float d0 = nestShell(a, 2.6, open * 2.6, s0);
    float d1 = nestShell(b, 1.75, open * 1.75, s1);
    float d2 = nestShell(c, 1.0, open * 1.0, s2);
    float d = min(d0, min(d1, d2));
    float seam = d0 < d1 ? s0 : s1;
    seam = d2 < min(d0, d1) ? s2 : seam;
    return vec2(d, seam);
}

vec2 prismMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0) || strataUses(1)) a = shells(p);
    if (strataUses(1)) {
        float holes = strataHoles(p, 2.75, gStrataIters);
        b = vec2(max(a.x, -holes), min(a.y, abs(holes)));
    }
    if (strataUses(2)) {
        // Shells within shells, each turned by its own place in the nest, every other one the other way.
        float s;
        float grow;
        float bound;
        vec3 q = strataNest(p, 2.6, 1.5, 2.0 + gStrataIters, s, grow, bound);
        float j = floor(log(s) / log(1.5) + 0.5);
        float way = mod(j, 2.0) * 2.0 - 1.0;
        q.xz = prismRot(1.3 * j + way * uTime * PRISM_RATE(50)) * q.xz;
        q.yz = prismRot(0.7 * j) * q.yz;
        float seam;
        float d = nestShell(q, 2.6, mix(0.18, 0.36, gPrismForm) * 2.6, seam);
        c = vec2(min((d + 0.5 * (1.0 - grow)) * s, bound), seam * s);
    }
    return strataBlend2(a, b, c);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    strataSetup(vec3(3.0, 1.0, 2.0), 2.0, 0.45, 0.7);
    vec3 ro;
    vec3 rd;
    camAround(uv, 7.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = prismFinish(prismTrace(ro, rd), 1.1);
}
