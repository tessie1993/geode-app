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

// Fold Nerve: a flight through a lattice of folded crystals seen only as
// their nerves of light. Space is mirrored into cells two wide, and in each
// it is folded, turned a little and grown about a corner, level after level.
// The threads are where the orbit passes close to its own axes; where four
// cells meet, the way ahead is kept clear.
//
// Three formulas (Formula, lib_layers), each a different fold rule:
//   0  the cube's nine mirrors to one wedge, doubled about a corner
//   1  a sponge: the same mirrors, tripled about a corner with the cross cut
//      out, so the nerves run round square windows
//   2  a tetrahedron's three mirrors, doubled about a corner: nerves in
//      triangles, a gasket of light
//
// The iteration count runs from 5 to 7 with Detail, k1 and k4, the last one
// mixed in by its share (lib_layers).
//
// Morph turns each level a little further, and the threads swing round into
// a new knotwork. Worst case 64 samples and 7 fold iterations, no noise.

vec4 nerveBody(vec3 p, int rule) {
    vec3 q = foldRepeat(p, 2.0);
    float s = 1.0;
    mat2 turn = foldRot(0.2 + 0.15 * gFoldForm);
    vec2 trap = vec2(1.0e3);
    float hue = 0.0;
    float n = layerIters(5.0, 7.0);
    for (int i = 0; i < 7; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0) break;
        vec3 f;
        float g = 2.0;
        if (rule == 1) {
            f = foldOcta(q);
            f.xz = turn * f.xz;
            f = 3.0 * f - vec3(2.0);
            // The cross: what lands past the cube along the least axis comes back a cube's width.
            if (f.z < -1.0) f.z += 2.0;
            g = 3.0;
        } else if (rule == 2) {
            f = foldMirror(q, vec3(0.70710678, 0.70710678, 0.0));
            f = foldMirror(f, vec3(0.70710678, 0.0, 0.70710678));
            f = foldMirror(f, vec3(0.0, 0.70710678, 0.70710678));
            f.xz = turn * f.xz;
            f = 2.0 * f - vec3(1.0);
        } else {
            f = foldOcta(q);
            f.xz = turn * f.xz;
            f = 2.0 * f - vec3(0.9, 0.65, 0.4);
        }
        q = mix(q, f, a);
        s *= mix(1.0, g, a);
        // The first three levels only: deeper ones are finer than any thread and would fill the lattice with fog.
        if (i < 3) trap = min(trap, vec2(length(q.yz), length(q.xy)) / s);
        if (i == 1) hue = 0.3 * length(q);
    }
    vec3 aq = abs(q);
    float d = rule == 1 ? (max(max(aq.x, aq.y), aq.z) - 1.0) / s : (length(q) - (rule == 2 ? 1.5 : 1.2)) / s;
    d = max(d, 0.65 - length(p.xy));
    return vec4(d, hue + 0.15 * sin(p.z * (FOLD_TAU / FOLD_PERIOD)) + 0.15 * float(rule), trap);
}

vec4 foldMap(vec3 p) {
    float slack = layerFlowWarp(p, gFoldFlow, gFoldChaos);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * nerveBody(p, 0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * nerveBody(p, 1);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * nerveBody(p, 2);
    m.x *= slack;
    return m;
}

//#include lib_fold_march_glow

void main() {
    vec2 uv = view();
    foldSetup();
    gFoldAxial = 1.0;
    gFoldDense = 0.35;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = foldFinish(foldGlow(foldEye(ro), rd), 1.3);
}
