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

// Fold Spire: a tetrahedron of tetrahedra standing on three feet, its fourth
// corner straight up. Space is folded by the tetrahedron's three mirrors,
// each hollow is rounded by a fold in a small ball, and the whole is doubled
// about the top corner and twisted about the upright, seven times, so every
// level turns a little on the one below. The surface is sphere traced to the
// pixel's cone and ruled with the orbit's seams. Turned about.
//
// Morph winds the twist tighter, from a straight-sided pyramid into a spiral
// tower. Worst case 60 steps and 5 more for the hit, 7 fold iterations, no
// noise.

vec4 foldMap(vec3 p) {
    p.xz = foldRot(uTime * FOLD_RATE(60)) * p.xz;
    // World y is the fractal's (1, 1, 1), so one corner points up; 1.1 of its units to a world unit.
    vec3 q = mat3(vec3(0.70710678, -0.70710678, 0.0), vec3(0.57735027), vec3(-0.40824829, -0.40824829, 0.81649658)) * p * 1.1;
    float s = 1.1;
    float tw = 0.1 + 0.2 * gFoldForm;
    float ct = cos(tw);
    float st = sin(tw);
    vec2 trap = vec2(1.0e3);
    float hue = 0.0;
    for (int i = 0; i < 7; i++) {
        q = foldMirror(q, vec3(0.70710678, 0.70710678, 0.0));
        q = foldMirror(q, vec3(0.70710678, 0.0, 0.70710678));
        q = foldMirror(q, vec3(0.0, 0.70710678, 0.70710678));
        // Inverted in the ball of radius 0.71, at most 2.5 times.
        float f = clamp(0.5 / max(dot(q, q), 1.0e-6), 1.0, 2.5);
        q *= f;
        s *= f;
        // Doubled about the top corner, which stays put, and twisted about the line through it.
        q = 2.0 * q - vec3(1.0);
        s *= 2.0;
        q = foldTwist(q, vec3(0.57735027), ct, st);
        if (i < 3) trap = min(trap, vec2(abs(q.x - q.y) * 0.70710678, abs(q.z)) / s);
        if (i == 2) hue = 0.25 * length(q);
    }
    return vec4((length(q) - 1.5) / s, hue, trap);
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
