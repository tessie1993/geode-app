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

// Fold Twin: one body grown by two fold rules taking turns. The first folds
// space back into a cube, turns it inside out in a ball, doubles it and adds
// the start, as a box-and-ball fractal does; the second folds it by the
// cube's nine mirrors and doubles it about a corner, as a crystal does. Each
// level undoes a little of the last, so the body is neither: rounded
// chambers with faceted spurs, and spurs with chambers in them. Its distance
// is only a local bound, so the pair march takes capped steps. Turned about.
//
// Morph moves the crystal rule's corner, and the spurs swing round the
// chambers. Worst case 60 steps and 5 more for the hit, 6 fold iterations
// (3 of each rule), no noise.

vec4 foldMap(vec3 p) {
    p.xz = foldRot(uTime * FOLD_RATE(50)) * p.xz;
    // 2.2 of the fractal's units to a world unit.
    vec3 c = p * 2.2;
    vec3 q = c;
    float s = 2.2;
    vec3 corner = vec3(1.0, 0.7 + 0.25 * gFoldForm, 0.4);
    vec2 trap = vec2(1.0e3);
    float hue = 0.0;
    for (int i = 0; i < 6; i++) {
        if (i % 2 == 0) {
            q = foldBox(q, 1.0);
            float f = foldBall(q, 0.25);
            q = 2.0 * f * q + c;
            s = min(s * 2.0 * f + 2.2, 1.0e8);
        } else {
            q = 2.0 * foldOcta(q) - corner;
            s = min(s * 2.0, 1.0e8);
        }
        if (i < 3) trap = min(trap, vec2(abs(q.y), abs(q.z)) / s);
        if (i == 1) hue = 0.2 * length(q);
        if (dot(q, q) > 1.0e4) break;
    }
    return vec4((length(q) - 1.0) / s, hue, trap);
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
