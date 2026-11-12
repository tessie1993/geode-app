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

// Prism Facet: a flight through clusters of mirror plates, one every four
// units of the way. Space is folded twice over, by the three planes of the
// axes and the diagonals between them, halved and turned at each fold, and a
// thin plate stands at both levels, so each cluster is a ring of large plates
// with a ring of small ones mirrored about each, a bore left open down its
// middle. Lines of light run where each fold's mirrors cut the plates.
//
// Morph turns every fold a little further, so the clusters re-cut themselves
// in place. The folds run gStrataIters times (1..3, with Detail and k1
// TEXTURE), the last one's plates growing in from nothing.
//
// Formula, three fold rules (lib_strata's kinds): 0 the mirrors of the axes
// and diagonals, halved and turned, as they always were; 0.5 a Mandelbox's
// box and sphere fold, the plates crumpled round each cluster's heart; 1 a
// Menger rule, the mirrors scaled by three about a cube's corners, so the
// plates stand on a sponge. The far fractal follows: a Menger cube, a
// kaleidoscope dust, a Mandelbox.
//
// Worst case 108 steps and 3 fold iterations, no noise; then the layers.

/** One cluster at p by fold rule `kind`: x the distance, y the ribbon's. */
vec2 facetCluster(vec3 p, float kind) {
    float zl = p.z - 4.0 * floor(p.z * 0.25 + 0.5);
    vec3 q = vec3(p.xy, zl);
    mat2 turn = prismRot(0.35 + 0.2 * gPrismForm + 0.12 * sin(uTime * PRISM_RATE(70)));
    float n = clamp(gStrataIters, 1.0, 3.0);
    float s = 1.0;
    float d = 1.0e3;
    float seam = 1.0e3;
    for (int i = 0; i < 3; i++) {
        float f = clamp(n - float(i), 0.0, 1.0);
        if (f <= 0.0) break;
        if (kind < 0.5) {
            q = abs(q);
            q.xy = q.x < q.y ? q.yx : q.xy;
            q.xz = q.x < q.z ? q.zx : q.xz;
            seam = min(seam, min(q.y, q.z) / s);
            q = 2.0 * q - vec3(2.2, 1.1, 0.9);
            s *= 2.0;
        } else if (kind < 1.5) {
            q = clamp(q, -1.0, 1.0) * 2.0 - q;
            float m = clamp(1.0 / max(dot(q, q), 1.0e-4), 1.0, 4.0);
            seam = min(seam, min(abs(q.y), abs(q.z)) / s);
            q = 2.0 * m * q - vec3(1.4, 0.7, 0.5);
            s *= 2.0 * m;
        } else {
            q = strataSort(abs(q));
            seam = min(seam, min(q.y, q.z) / s);
            q = 3.0 * q - vec3(2.4, 2.4, 2.0);
            q.z += 2.0 * step(q.z, -1.0);
            s *= 3.0;
        }
        q.xy = turn * q.xy;
        // The plate of this level, as thick as the level has grown in.
        vec3 b = abs(q) - vec3(1.0, 0.07 * f, 0.6);
        d = min(d, (length(max(b, 0.0)) + min(max(b.x, max(b.y, b.z)), 0.0)) / s);
    }
    // The bore down the middle, and each cluster kept inside its own four units.
    d = max(d, max(0.8 - length(p.xy), abs(zl) - 1.7));
    d = min(d, 2.3 - abs(zl));
    return vec2(d, seam);
}

vec2 prismMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = facetCluster(p, 0.0);
    if (strataUses(1)) b = facetCluster(p, 1.0);
    if (strataUses(2)) c = facetCluster(p, 2.0);
    return strataBlend2(a, b, c);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    strataSetup(vec3(3.0, 0.0, 1.0), 1.0, 0.45, 0.7);
    gPrismStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.0);
}
