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

// Fold Onion: a quaternion Julia set, a fractal in four dimensions, cut by
// sixteen nested spheres. Each sphere is a cut at its own depth in the fourth
// dimension, deeper the further in, so the shells are sixteen different
// sections of the one body: outlined where its edge crosses, filled faintly
// inside, laid over each other front to back like the skins of an onion of
// glass. Turned about.
//
// Morph moves the cuts through the fourth dimension, and the sections bud,
// join and part. Worst case 32 sections and 10 iterations, no noise.

vec4 foldMap(vec3 p) {
    p.xz = foldRot(uTime * FOLD_RATE(40)) * p.xz;
    vec4 z = vec4(p, mix(-0.3, 0.3, gFoldForm) + 0.35 * (length(p) - 0.75));
    vec4 c = vec4(-0.29, -0.4, 0.34, 0.44) + 0.04 * vec4(cos(uTime * FOLD_RATE(90)), sin(uTime * FOLD_RATE(90)), 0.0, 0.0);
    // The orbit's derivative's length: the quaternion norm is multiplicative, so 2 |z| per square.
    float md = 1.0;
    float m2 = dot(z, z);
    float least = m2;
    vec2 trap = vec2(1.0e3);
    for (int i = 0; i < 10; i++) {
        md = min(2.0 * sqrt(m2) * md, 1.0e20);
        z = foldQuatSq(z) + c;
        m2 = dot(z, z);
        least = min(least, m2);
        if (i < 3) trap = min(trap, vec2(abs(z.x), abs(z.w)) / max(md, 1.0e-6));
        if (m2 > 256.0) break;
    }
    float r = sqrt(m2);
    float d = clamp(0.5 * r * log(max(r, 1.0e-6)) / max(md, 1.0e-6), -1.0, 4.0);
    return vec4(d, 0.6 * sqrt(least), trap);
}

//#include lib_fold_march_slice

void main() {
    vec2 uv = view();
    foldSetup();
    gFoldShellR = 1.5;
    gFoldShells = 16.0;
    gFoldDense = 0.35;
    gFoldFar = 6.4 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.2, CAM_SPIN_UNIT, ro, rd);
    fragColor = foldFinish(foldSliceShells(ro, rd), 1.3);
}
