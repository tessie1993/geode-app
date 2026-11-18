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

// Fold Lace: a box-and-ball fractal (space folded back into a cube, turned
// inside out in a ball, scaled and the start added) seen only as light. No
// surface: the threads where the orbit runs close to two axes, gathered along
// the ray, and a faint haze inside the body that dims the far threads behind
// the near. Turned about.
//
// Three formulas (Formula, lib_layers), each a different fold rule:
//   0  the box fold at scale minus 1.85: open lace
//   1  the box fold across two axes only, the third left to run: a curling
//      sheet of lace, layered like folded paper
//   2  the box fold at scale plus 2: a walled city of lace, boxes in boxes
//
// The iteration count runs from 5 to 7 with Detail, k1 and k4, the last one
// mixed in by its share, so it grows without a jump (lib_layers).
//
// Morph widens the ball space is turned inside out in, and the lace opens
// into wider loops. Worst case 64 samples and 7 fold iterations, no noise.

vec4 laceBody(vec3 p, int rule) {
    // 2.4 of the fractal's units to a world unit.
    vec3 c = p * 2.4;
    vec3 z = c;
    float dr = 2.4;
    float inner = mix(0.2, 0.4, gFoldForm);
    float scale = rule == 2 ? 2.0 : -1.85;
    vec2 trap = vec2(1.0e3);
    float n = layerIters(5.0, 7.0);
    for (int i = 0; i < 7; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0) break;
        vec3 b = foldBox(z, 1.0);
        if (rule == 1) b.z = z.z;
        float f = foldBall(b, inner);
        z = mix(z, scale * f * b + c, a);
        dr = mix(dr, min(dr * abs(scale) * f + 2.4, 1.0e8), a);
        // The first three levels only: deeper ones are finer than any thread and would fill the body with fog.
        if (i < 3) trap = min(trap, vec2(length(z.xy), length(z.yz)) / dr);
        if (dot(z, z) > 1.0e4) break;
    }
    return vec4(length(z) / dr, 0.12 * log(dr) + 0.2 * float(rule), trap);
}

vec4 foldMap(vec3 p) {
    p.xz = foldRot(uTime * FOLD_RATE(70)) * p.xz;
    float slack = layerFlowWarp(p, gFoldFlow, gFoldChaos);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * laceBody(p, 0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * laceBody(p, 1);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * laceBody(p, 2);
    m.x *= slack;
    return m;
}

//#include lib_fold_march_glow

void main() {
    vec2 uv = view();
    foldSetup();
    gFoldEdge = 0.3;
    gFoldDense = 0.6;
    gFoldFar = 9.0 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 5.2, CAM_SPIN_UNIT, ro, rd);
    fragColor = foldFinish(foldGlow(ro, rd), 1.6);
}
