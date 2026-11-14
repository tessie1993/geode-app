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

// Fold Spire: a tower of folded solids standing on its feet, one corner
// straight up. Space is folded by mirrors, rounded in a small ball, and the
// whole is doubled about the top corner and twisted about the upright, level
// after level, so every level turns a little on the one below. The surface is
// sphere traced to the pixel's cone and ruled with the orbit's seams. Turned
// about.
//
// Three formulas (Formula, lib_layers), each a different fold:
//   0  the tetrahedron's three mirrors: a tetrahedron of tetrahedra
//   1  the cube's nine mirrors: a stack of octahedra, square in plan
//   2  the tetrahedron's mirrors with a deep inversion (up to six times, in
//      a wider ball) and no twist: a spire of bubbles, apollonian at its
//      joins
//
// The iteration count runs from 5 to 7 with Detail, k1 and k4, the last one
// mixed in by its share (lib_layers).
//
// Morph winds the twist tighter, from a straight-sided pyramid into a spiral
// tower. Worst case 60 steps and 5 more for the hit, 7 fold iterations, no
// noise.

vec4 spireBody(vec3 p, int rule) {
    // World y is the fractal's (1, 1, 1), so one corner points up; 1.1 of its units to a world unit.
    vec3 q = mat3(vec3(0.70710678, -0.70710678, 0.0), vec3(0.57735027), vec3(-0.40824829, -0.40824829, 0.81649658)) * p * 1.1;
    float s = 1.1;
    float tw = rule == 2 ? 0.0 : 0.1 + 0.2 * gFoldForm;
    float ct = cos(tw);
    float st = sin(tw);
    // The ball: radius root(0.5) and at most 2.5 times, or for the bubbles root(0.9) and at most 6 times.
    float ball = rule == 2 ? 0.9 : 0.5;
    float most = rule == 2 ? 6.0 : 2.5;
    vec2 trap = vec2(1.0e3);
    float hue = 0.0;
    float n = layerIters(5.0, 7.0);
    for (int i = 0; i < 7; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0) break;
        vec3 f;
        if (rule == 1) {
            f = foldOcta(q);
        } else {
            f = foldMirror(q, vec3(0.70710678, 0.70710678, 0.0));
            f = foldMirror(f, vec3(0.70710678, 0.0, 0.70710678));
            f = foldMirror(f, vec3(0.0, 0.70710678, 0.70710678));
        }
        float k = clamp(ball / max(dot(f, f), 1.0e-6), 1.0, most);
        // Doubled about the top corner, which stays put, and twisted about the line through it.
        f = foldTwist(2.0 * f * k - vec3(1.0), vec3(0.57735027), ct, st);
        q = mix(q, f, a);
        s *= mix(1.0, 2.0 * k, a);
        if (i < 3) trap = min(trap, vec2(abs(q.x - q.y) * 0.70710678, abs(q.z)) / s);
        if (i == 2) hue = 0.25 * length(q);
    }
    return vec4((length(q) - 1.5) / s, hue + 0.2 * float(rule), trap);
}

vec4 foldMap(vec3 p) {
    p.xz = foldRot(uTime * FOLD_RATE(60)) * p.xz;
    float slack = layerFlowWarp(p, gFoldFlow, gFoldChaos);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * spireBody(p, 0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * spireBody(p, 1);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * spireBody(p, 2);
    m.x *= slack;
    return m;
}

//#include lib_fold_march_cone

void main() {
    vec2 uv = view();
    foldSetup();
    gFoldFar = 8.0 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.6, CAM_SPIN_UNIT, ro, rd);
    fragColor = foldFinish(foldCone(ro, rd), 1.3);
}
