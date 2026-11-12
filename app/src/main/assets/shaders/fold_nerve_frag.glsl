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
// the cube's nine mirrors fold it to one wedge, turn it a little and double
// it about a corner, seven times. The threads are where the orbit passes
// close to its own axes; where four cells meet, the way ahead is kept clear.
//
// Morph turns each level a little further, and the threads swing round into
// a new knotwork. Worst case 64 samples and 7 fold iterations, no noise.

vec4 foldMap(vec3 p) {
    vec3 q = foldRepeat(p, 2.0);
    float s = 1.0;
    mat2 turn = foldRot(0.2 + 0.15 * gFoldForm);
    vec2 trap = vec2(1.0e3);
    float hue = 0.0;
    for (int i = 0; i < 7; i++) {
        q = foldOcta(q);
        q.xz = turn * q.xz;
        q = 2.0 * q - vec3(0.9, 0.65, 0.4);
        s *= 2.0;
        // The first three levels only: deeper ones are finer than any thread and would fill the lattice with fog.
        if (i < 3) trap = min(trap, vec2(length(q.yz), length(q.xy)) / s);
        if (i == 1) hue = 0.3 * length(q);
    }
    float d = (length(q) - 1.2) / s;
    d = max(d, 0.65 - length(p.xy));
    return vec4(d, hue + 0.15 * sin(p.z * (FOLD_TAU / FOLD_PERIOD)), trap);
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
