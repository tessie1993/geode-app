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

// Fold Twin: one body grown by two fold rules taking turns, each level
// undoing a little of the last, so the body is neither: rounded chambers with
// faceted spurs, and spurs with chambers in them. Its distance is only a local
// bound, so the pair march takes capped steps. Turned about.
//
// Three formulas (Formula, lib_layers), each a different pair of rules; the
// first of each pair folds space back into a cube (or only into a ball), turns
// it inside out in a ball, doubles it and adds the start:
//   0  box and ball, then the cube's nine mirrors doubled about a corner:
//      chambers with crystal spurs
//   1  box and ball, then the tetrahedron's three mirrors doubled about a
//      corner: chambers with three-sided spines
//   2  the ball alone, then a sponge's tripling with the cross cut out:
//      bubbles pierced by square windows
//
// The iteration count runs from 4 to 6 with Detail, k1 and k4, the last one
// mixed in by its share (lib_layers).
//
// Morph moves the crystal rule's corner, and the spurs swing round the
// chambers. Worst case 60 steps and 5 more for the hit, 6 fold iterations
// (3 of each rule), no noise.

vec4 twinBody(vec3 p, int rule) {
    // 2.2 of the fractal's units to a world unit.
    vec3 c = p * 2.2;
    vec3 q = c;
    float s = 2.2;
    vec3 corner = vec3(1.0, 0.7 + 0.25 * gFoldForm, 0.4);
    vec2 trap = vec2(1.0e3);
    float hue = 0.0;
    float n = layerIters(4.0, 6.0);
    for (int i = 0; i < 6; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0) break;
        vec3 f;
        float sn;
        if (i % 2 == 0) {
            vec3 b = rule == 2 ? q : foldBox(q, 1.0);
            float k = foldBall(b, 0.25);
            f = 2.0 * k * b + c;
            sn = min(s * 2.0 * k + 2.2, 1.0e8);
        } else if (rule == 1) {
            f = foldMirror(q, vec3(0.70710678, 0.70710678, 0.0));
            f = foldMirror(f, vec3(0.70710678, 0.0, 0.70710678));
            f = foldMirror(f, vec3(0.0, 0.70710678, 0.70710678));
            f = 2.0 * f - corner;
            sn = min(s * 2.0, 1.0e8);
        } else if (rule == 2) {
            f = 3.0 * foldOcta(q) - vec3(2.0 * corner.x, 2.0 * corner.y, 2.0);
            if (f.z < -1.0) f.z += 2.0;
            sn = min(s * 3.0, 1.0e8);
        } else {
            f = 2.0 * foldOcta(q) - corner;
            sn = min(s * 2.0, 1.0e8);
        }
        q = mix(q, f, a);
        s = mix(s, sn, a);
        if (i < 3) trap = min(trap, vec2(abs(q.y), abs(q.z)) / s);
        if (i == 1) hue = 0.2 * length(q);
        if (dot(q, q) > 1.0e4) break;
    }
    return vec4((length(q) - 1.0) / s, hue + 0.2 * float(rule), trap);
}

vec4 foldMap(vec3 p) {
    p.xz = foldRot(uTime * FOLD_RATE(50)) * p.xz;
    float slack = layerFlowWarp(p, gFoldFlow, gFoldChaos);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * twinBody(p, 0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * twinBody(p, 1);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * twinBody(p, 2);
    m.x *= slack;
    return m;
}

//#include lib_fold_march_pair

void main() {
    vec2 uv = view();
    foldSetup();
    gFoldStride = 0.7;
    gFoldCap = 0.15;
    gFoldFar = 8.5 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 5.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = foldFinish(foldPair(ro, rd), 1.3);
}
