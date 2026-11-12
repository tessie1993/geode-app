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

// Fold Scaffold: a flight through a scaffold grown by two fold rules taking
// turns, in mirrored cells two wide. The first folds space by the cube's nine
// mirrors and triples it about a corner with a cross cut out, as a sponge
// does; the second folds every sign to plus and by the tetrahedron's three
// mirrors, and doubles it about a corner. So square windows open onto
// triangular ones and back, level by level. Its distance is only a local
// bound, so the pair march takes capped steps; the way ahead is kept clear.
//
// Morph moves the sponge's corner, and the windows widen and narrow.
// Worst case 60 steps and 5 more for the hit, 6 fold iterations (3 of each
// rule), no noise.

vec4 foldMap(vec3 p) {
    vec3 q = foldRepeat(p, 2.0);
    float s = 1.0;
    float open = 2.0 - 0.15 * gFoldForm;
    vec2 trap = vec2(1.0e3);
    float hue = 0.0;
    for (int i = 0; i < 6; i++) {
        if (i % 2 == 0) {
            q = 3.0 * foldOcta(q) - vec3(open, open, 2.0);
            // The cross: what lands past the cube along the least axis comes back a cube's width. The jump
            // is from -1 to 1, which the next rule's abs() joins up again.
            if (q.z < -1.0) q.z += 2.0;
            s *= 3.0;
        } else {
            q = abs(q);
            q = foldMirror(q, vec3(0.70710678, 0.70710678, 0.0));
            q = foldMirror(q, vec3(0.70710678, 0.0, 0.70710678));
            q = foldMirror(q, vec3(0.0, 0.70710678, 0.70710678));
            q = 2.0 * q - vec3(1.0);
            s *= 2.0;
        }
        if (i < 3) trap = min(trap, vec2(abs(q.x), abs(q.y)) / s);
        if (i == 1) hue = 0.2 * length(q);
    }
    vec3 a = abs(q);
    float d = (max(max(a.x, a.y), a.z) - 1.0) / s;
    d = max(d, 0.7 - length(p.xy));
    return vec4(d, hue + 0.15 * sin(p.z * (FOLD_TAU / FOLD_PERIOD)), trap);
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
