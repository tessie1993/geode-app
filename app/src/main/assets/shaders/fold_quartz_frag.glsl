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

// Fold Quartz: a flight between clusters of fractal crystal, one to each
// mirrored cell two wide. In each, space is box folded, then squared the
// quaternion way with its two side axes turned over, and a fixed point added,
// seven times: a Julia set of a square that is not conformal, so it has no
// distance to trust. The crystal is the level of the escape potential, found
// by the probe march; the way ahead is kept clear.
//
// Morph moves the fixed point, and the crystals grow new faces and lose old
// ones. Worst case 52 strides and 10 more for the hit, 7 iterations, no noise.

vec4 foldMap(vec3 p) {
    // 1.4 of the fractal's units to a world unit.
    vec3 z = foldRepeat(p, 2.0) * 1.4;
    vec3 c = vec3(-0.55, 0.42, -0.3) + gFoldForm * vec3(0.12, -0.1, 0.08);
    float m2 = dot(z, z);
    float md = 1.4;
    // 2 to the minus the number of squares so far.
    float fall = 1.0;
    float least = m2;
    vec2 trap = vec2(1.0e3);
    for (int i = 0; i < 7; i++) {
        z = foldBox(z, 0.85);
        md = min(2.0 * length(z) * md, 1.0e20);
        z = vec3(z.x * z.x - z.y * z.y - z.z * z.z, -2.0 * z.x * z.y, -2.0 * z.x * z.z) + c;
        m2 = dot(z, z);
        fall *= 0.5;
        least = min(least, m2);
        if (i < 3) trap = min(trap, vec2(abs(z.x), abs(z.y)) / max(md, 1.0e-6));
        if (m2 > 1.0e4) break;
    }
    // The escape potential; the crystal is where it is under 0.05. Over its rough gradient (the
    // derivative's length, not a bound), so the field is about a world unit per unit near the crystal.
    float g = 0.5 * log(max(m2, 1.0)) * fall;
    float grad = fall * md / sqrt(max(m2, 1.0));
    float f = clamp((g - 0.05) / max(grad, 1.0e-3), -1.0, 1.0);
    f = max(f, 0.75 - length(p.xy));
    return vec4(f, 0.5 * sqrt(least) + 0.15 * sin(p.z * (FOLD_TAU / FOLD_PERIOD)), trap);
}

//#include lib_fold_march_probe

void main() {
    vec2 uv = view();
    foldSetup();
    gFoldAxial = 1.0;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = foldFinish(foldProbe(foldEye(ro), rd), 1.3);
}
