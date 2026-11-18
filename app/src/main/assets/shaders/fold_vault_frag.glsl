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
//#include lib_fold

// Fold Vault: a flight down a nave whose vaulting is grown, not built. Space
// is mirrored in its three planes, turned inside out in the unit ball and
// shifted, level after level, and every level leaves one primitive behind;
// carried back through the inversions, each comes out curved, so arches
// spring from arches at every size. A hull closes the nave round, the way
// down its middle stays clear, and the bays repeat every four units.
//
// Three formulas (Formula, lib_layers), each a different primitive left at
// every level:
//   0  a thin slab: curved ribs
//   1  a thin tube: branching tracery, like the ribs of a fan vault
//   2  a slab pierced by a round hole (the tube cut out of it): ribs
//      perforated with rose windows
//
// The iteration count runs from 4 to 6 with Detail, k1 and k4, the last level
// mixed in by its share (lib_layers).
//
// Morph shifts the folds, and the ribs lean and branch anew. Worst case 60
// steps and 5 more for the hit, 6 fold iterations, no noise.

vec4 vaultBody(vec3 p, int rule) {
    // 0.55 of the fractal's units to a world unit; the bays mirror along the way.
    vec3 q = vec3(p.xy, foldRepeat(p, 2.0).z) * 0.55;
    float s = 0.55;
    vec3 c = vec3(0.62 + 0.12 * gFoldForm, 0.78, 0.55 - 0.1 * gFoldForm);
    float d = 1.0e3;
    vec2 trap = vec2(1.0e3);
    float n = layerIters(4.0, 6.0);
    for (int i = 0; i < 6; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0) break;
        vec3 f = abs(q);
        // The inversion, its radius held off zero: inside 0.28 it only scales.
        float r2 = max(dot(f, f), 0.08);
        f = f / r2 - c;
        q = mix(q, f, a);
        s = mix(s, clamp(s / r2, 1.0e-6, 1.0e6), a);
        float slab = abs(q.y) - 0.08;
        float tube = length(q.xz) - 0.1;
        float prim = rule == 1 ? tube : (rule == 2 ? max(slab, 0.22 - length(q.xz)) : slab);
        // The newest level joins in by its share.
        d = mix(d, min(d, prim / s), a);
        if (i < 3) trap = min(trap, vec2(abs(q.x), abs(q.z)) / s);
    }
    float r = length(p.xy);
    // The hull beyond 3, the way within 0.7.
    d = max(min(d, 3.0 - r), 0.7 - r);
    return vec4(d, 0.12 * log(1.0 + s) + 0.2 * float(rule), trap);
}

vec4 foldMap(vec3 p) {
    float slack = layerFlowWarp(p, gFoldFlow, gFoldChaos);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * vaultBody(p, 0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * vaultBody(p, 1);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * vaultBody(p, 2);
    m.x *= slack;
    return m;
}

//#include lib_fold_march_cone

void main() {
    vec2 uv = view();
    foldSetup();
    gFoldAxial = 1.0;
    gFoldStride = 0.6;
    gFoldFar = 13.0 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = foldFinish(foldCone(foldEye(ro), rd), 1.4);
}
