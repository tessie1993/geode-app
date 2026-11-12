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
// shifted, six times over, and every level leaves one thin slab; carried back
// through the inversions, each slab comes out as a curved rib, so arches
// spring from arches at every size. A hull closes the nave round, the way
// down its middle stays clear, and the bays repeat every four units.
//
// Morph shifts the folds, and the ribs lean and branch anew. Worst case 60
// steps and 5 more for the hit, 6 fold iterations, no noise.

vec4 foldMap(vec3 p) {
    // 0.55 of the fractal's units to a world unit; the bays mirror along the way.
    vec3 q = vec3(p.xy, foldRepeat(p, 2.0).z) * 0.55;
    float s = 0.55;
    vec3 c = vec3(0.62 + 0.12 * gFoldForm, 0.78, 0.55 - 0.1 * gFoldForm);
    float d = 1.0e3;
    vec2 trap = vec2(1.0e3);
    for (int i = 0; i < 6; i++) {
        q = abs(q);
        // The inversion, its radius held off zero: inside 0.28 it only scales.
        float r2 = max(dot(q, q), 0.08);
        q /= r2;
        s = clamp(s / r2, 1.0e-6, 1.0e6);
        q -= c;
        d = min(d, (abs(q.y) - 0.08) / s);
        if (i < 3) trap = min(trap, vec2(abs(q.x), abs(q.z)) / s);
    }
    float r = length(p.xy);
    // The hull beyond 3, the way within 0.7.
    d = max(min(d, 3.0 - r), 0.7 - r);
    return vec4(d, 0.12 * log(1.0 + s), trap);
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
