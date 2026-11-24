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

// Fold Onion: a fractal cut by sixteen nested spheres. Each sphere is a cut
// at its own depth, deeper the further in, so the shells are sixteen
// different sections of the one body: outlined where its edge crosses,
// filled faintly inside, laid over each other front to back like the skins
// of an onion of glass. Turned about.
//
// Three formulas (Formula, lib_layers), each a different body:
//   0  a quaternion Julia set, squared, cut through its fourth dimension
//   1  a cubic quaternion Julia set: three lobes where the square has two,
//      so the sections bud in threes
//   2  a Mandelbulb of power six, each shell a section at its own turn of
//      the bulb's latitude: ridged petals that wind as they go in
//
// The iteration count grows with Detail, k1 and k4 (7 to 10 for the Julia
// sets, 4 to 6 for the bulb), the last one mixed in by its share (lib_layers).
//
// Morph moves the cuts through the body, and the sections bud, join and
// part. Worst case 32 sections and 10 iterations, no noise.

vec4 onionMul(vec4 a, vec4 b) {
    return vec4(a.x * b.x - dot(a.yzw, b.yzw), a.x * b.yzw + b.x * a.yzw + cross(a.yzw, b.yzw));
}

vec4 onionJulia(vec3 p, bool cubic) {
    vec4 z = vec4(p, mix(-0.3, 0.3, gFoldForm) + 0.35 * (length(p) - 0.75));
    vec4 c = cubic ? vec4(-0.18, 0.52, 0.25, -0.2) : vec4(-0.29, -0.4, 0.34, 0.44);
    c += 0.04 * vec4(cos(uTime * FOLD_RATE(90)), sin(uTime * FOLD_RATE(90)), 0.0, 0.0);
    // The orbit's derivative's length: the quaternion norm is multiplicative, so n |z|^(n-1) per power.
    float md = 1.0;
    float m2 = dot(z, z);
    float least = m2;
    vec2 trap = vec2(1.0e3);
    float n = layerIters(7.0, 10.0);
    for (int i = 0; i < 10; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0) break;
        vec4 sq = foldQuatSq(z);
        vec4 zn = cubic ? onionMul(z, sq) + c : sq + c;
        float mdn = cubic ? 3.0 * m2 * md : 2.0 * sqrt(m2) * md;
        z = mix(z, zn, a);
        md = min(mix(md, mdn, a), 1.0e20);
        m2 = dot(z, z);
        least = min(least, m2);
        if (i < 3) trap = min(trap, vec2(abs(z.x), abs(z.w)) / max(md, 1.0e-6));
        if (m2 > 256.0) break;
    }
    float r = sqrt(m2);
    float d = clamp(0.5 * r * log(max(r, 1.0e-6)) / max(md, 1.0e-6), -1.0, 4.0);
    return vec4(d, 0.6 * sqrt(least), trap);
}

vec4 onionBulb(vec3 p) {
    // 1.25 of the bulb's units to a world unit; each shell's latitude turned by its depth.
    vec3 c = p * 1.25;
    vec3 w = c;
    float dr = 1.0;
    float r = length(w);
    float least = r;
    float lean = mix(-0.4, 0.4, gFoldForm) + 0.6 * (length(p) - 0.75);
    vec2 trap = vec2(1.0e3);
    float n = layerIters(4.0, 6.0);
    for (int i = 0; i < 6; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0 || r > 2.0) break;
        float th = 6.0 * acos(clamp(w.y / max(r, 1.0e-6), -1.0, 1.0)) + lean;
        float ph = 6.0 * atan(w.z, abs(w.x) + abs(w.z) > 1.0e-6 ? w.x : 1.0);
        float r2 = r * r;
        float r5 = r2 * r2 * r;
        vec3 wn = r5 * r * vec3(sin(th) * cos(ph), cos(th), sin(th) * sin(ph)) + c;
        dr = mix(dr, min(6.0 * r5 * dr + 1.0, 1.0e20), a);
        w = mix(w, wn, a);
        r = length(w);
        least = min(least, r);
        if (i < 3) trap = min(trap, vec2(abs(w.y), abs(w.x)) / max(dr, 1.0e-6));
    }
    float d = clamp(0.5 * log(max(r, 1.0e-6)) * r / max(dr, 1.0e-6), -1.0, 4.0) / 1.25;
    return vec4(d, 0.5 * least + 0.3, trap / 1.25);
}

vec4 foldMap(vec3 p) {
    p.xz = foldRot(uTime * FOLD_RATE(40)) * p.xz;
    float slack = layerFlowWarp(p, gFoldFlow, gFoldChaos);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * onionJulia(p, false);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * onionJulia(p, true);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * onionBulb(p);
    m.x *= slack;
    return m;
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
