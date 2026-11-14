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

// Fold Scaffold: a flight through a scaffold grown by fold rules, in mirrored
// cells two wide, the way ahead kept clear. The sponge rule folds space by the
// cube's nine mirrors and triples it about a corner with a cross cut out; the
// gasket rule folds it by the tetrahedron's three mirrors and doubles it
// about a corner. Its distance is only a local bound, so the pair march
// takes capped steps.
//
// Three formulas (Formula, lib_layers), each a different iteration rule:
//   0  the two rules taking turns (the gasket's mirrors after a fold of every
//      sign to plus), so square windows open onto triangular ones and back
//   1  the sponge alone: square windows within square windows
//   2  the gasket alone, its mirrors facing both ways: a lattice of
//      tetrahedra, triangle windows within triangles
//
// The iteration count runs from 4 to 6 with Detail, k1 and k4, the last one
// mixed in by its share (lib_layers).
//
// Morph moves the sponge's corner, and the windows widen and narrow.
// Worst case 60 steps and 5 more for the hit, 6 fold iterations, no noise.

vec3 scaffoldSponge(vec3 q, float open) {
    q = 3.0 * foldOcta(q) - vec3(open, open, 2.0);
    // The cross: what lands past the cube along the least axis comes back a cube's width. The jump
    // is from -1 to 1, which the next fold's abs() joins up again.
    if (q.z < -1.0) q.z += 2.0;
    return q;
}

vec3 scaffoldGasket(vec3 q, bool plus) {
    if (plus) q = abs(q);
    q = foldMirror(q, vec3(0.70710678, 0.70710678, 0.0));
    q = foldMirror(q, vec3(0.70710678, 0.0, 0.70710678));
    q = foldMirror(q, vec3(0.0, 0.70710678, 0.70710678));
    return 2.0 * q - vec3(1.0);
}

vec4 scaffoldBody(vec3 p, int rule) {
    vec3 q = foldRepeat(p, 2.0);
    float s = 1.0;
    float open = 2.0 - 0.15 * gFoldForm;
    vec2 trap = vec2(1.0e3);
    float hue = 0.0;
    float n = layerIters(4.0, 6.0);
    for (int i = 0; i < 6; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0) break;
        bool sponge = rule == 1 || (rule == 0 && i % 2 == 0);
        vec3 f = sponge ? scaffoldSponge(q, open) : scaffoldGasket(q, rule == 0);
        q = mix(q, f, a);
        s *= mix(1.0, sponge ? 3.0 : 2.0, a);
        if (i < 3) trap = min(trap, vec2(abs(q.x), abs(q.y)) / s);
        if (i == 1) hue = 0.2 * length(q);
    }
    vec3 aq = abs(q);
    float d = (max(max(aq.x, aq.y), aq.z) - 1.0) / s;
    d = max(d, 0.7 - length(p.xy));
    return vec4(d, hue + 0.15 * sin(p.z * (FOLD_TAU / FOLD_PERIOD)) + 0.2 * float(rule), trap);
}

vec4 foldMap(vec3 p) {
    float slack = layerFlowWarp(p, gFoldFlow, gFoldChaos);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * scaffoldBody(p, 0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * scaffoldBody(p, 1);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * scaffoldBody(p, 2);
    m.x *= slack;
    return m;
}

//#include lib_fold_march_pair

void main() {
    vec2 uv = view();
    foldSetup();
    gFoldAxial = 1.0;
    gFoldStride = 0.7;
    gFoldCap = 0.12;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = foldFinish(foldPair(foldEye(ro), rd), 1.4);
}
