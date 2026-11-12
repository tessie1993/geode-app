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
//#include lib_prism

// Prism Facet: a flight through clusters of mirror plates, one every four
// units of the way. Space is folded twice over, by the three planes of the
// axes and the diagonals between them, halved and turned at each fold, and a
// thin plate stands at both levels, so each cluster is a ring of large plates
// with a ring of small ones mirrored about each, a bore left open down its
// middle. Lines of light run where each fold's mirrors cut the plates.
//
// Morph turns every fold a little further, so the clusters re-cut themselves
// in place. Worst case 108 steps and 2 fold iterations, no noise.

vec2 prismMap(vec3 p) {
    float zl = p.z - 4.0 * floor(p.z * 0.25 + 0.5);
    vec3 q = vec3(p.xy, zl);
    mat2 turn = prismRot(0.35 + 0.2 * gPrismForm + 0.12 * sin(uTime * PRISM_RATE(70)));
    float s = 1.0;
    float d = 1.0e3;
    float seam = 1.0e3;
    for (int i = 0; i < 2; i++) {
        q = abs(q);
        q.xy = q.x < q.y ? q.yx : q.xy;
        q.xz = q.x < q.z ? q.zx : q.xz;
        seam = min(seam, min(q.y, q.z) / s);
        q = 2.0 * q - vec3(2.2, 1.1, 0.9);
        s *= 2.0;
        q.xy = turn * q.xy;
        vec3 b = abs(q) - vec3(1.0, 0.07, 0.6);
        d = min(d, (length(max(b, 0.0)) + min(max(b.x, max(b.y, b.z)), 0.0)) / s);
    }
    // The bore down the middle, and each cluster kept inside its own four units.
    d = max(d, max(0.8 - length(p.xy), abs(zl) - 1.7));
    d = min(d, 2.3 - abs(zl));
    return vec2(d, seam);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    gPrismStride = 0.8;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.0);
}
