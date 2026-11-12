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
// inside out in a ball, scaled by minus 1.85 and the start added, seven
// times) seen only as light. No surface: the threads where the orbit runs
// close to two axes, gathered along the ray, and a faint haze inside the
// body that dims the far threads behind the near. Turned about.
//
// Morph widens the ball space is turned inside out in, and the lace opens
// into wider loops. Worst case 64 samples and 7 fold iterations, no noise.

vec4 foldMap(vec3 p) {
    p.xz = foldRot(uTime * FOLD_RATE(70)) * p.xz;
    // 2.4 of the fractal's units to a world unit.
    vec3 c = p * 2.4;
    vec3 z = c;
    float dr = 2.4;
    float inner = mix(0.2, 0.4, gFoldForm);
    vec2 trap = vec2(1.0e3);
    for (int i = 0; i < 7; i++) {
        z = foldBox(z, 1.0);
        float f = foldBall(z, inner);
        z = -1.85 * f * z + c;
        dr = min(dr * 1.85 * f + 2.4, 1.0e8);
        // The first three levels only: deeper ones are finer than any thread and would fill the body with fog.
        if (i < 3) trap = min(trap, vec2(length(z.xy), length(z.yz)) / dr);
        if (dot(z, z) > 1.0e4) break;
    }
    return vec4(length(z) / dr, 0.12 * log(dr), trap);
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
