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

// Fold Thorn: every coordinate folded to plus, then squared the quaternion
// way and the start added, seven times: the plane's burning-ship map carried
// into space, so keels and thorns grow along every fold. The square is not
// conformal, so the usual distance estimate is no bound; the body is the
// level of the escape potential instead, and the probe march finds it by its
// change of sign. Turned about.
//
// Morph draws what is added toward one fixed point, from the ship toward its
// Julia relatives, and the thorns lengthen and twist. Worst case 52 strides
// and 10 more for the hit, 7 iterations, no noise.

vec4 foldMap(vec3 p) {
    p.xz = foldRot(uTime * FOLD_RATE(55)) * p.xz;
    // 1.15 of the fractal's units to a world unit, the body's middle at the origin.
    vec3 c = p * 1.15 + vec3(-0.45, -0.35, -0.35);
    vec3 add = mix(c, vec3(-1.05, -0.3, -0.25), 0.35 * gFoldForm);
    vec3 z = c;
    float m2 = dot(z, z);
    float md = 1.0;
    // 2 to the minus the number of squares so far.
    float fall = 1.0;
    float least = m2;
    vec2 trap = vec2(1.0e3);
    for (int i = 0; i < 7; i++) {
        md = min(2.0 * sqrt(m2) * md + 1.0, 1.0e20);
        z = abs(z);
        z = vec3(z.x * z.x - z.y * z.y - z.z * z.z, 2.0 * z.x * z.y, 2.0 * z.x * z.z) + add;
        m2 = dot(z, z);
        fall *= 0.5;
        least = min(least, m2);
        if (i < 3) trap = min(trap, vec2(abs(z.y), abs(z.z)) / (1.15 * md));
        if (m2 > 1.0e4) break;
    }
    // The escape potential; the body is where it is under 0.05. Over its rough gradient (the derivative's
    // length, not a bound), so the field is about a world unit per unit near the body.
    float g = 0.5 * log(max(m2, 1.0)) * fall;
    float grad = 1.15 * fall * md / sqrt(max(m2, 1.0));
    float f = clamp((g - 0.05) / max(grad, 1.0e-3), -1.0, 1.0);
    return vec4(f, 0.5 * sqrt(least), trap);
}

//#include lib_fold_march_probe

void main() {
    vec2 uv = view();
    foldSetup();
    gFoldFar = 7.5 * (1.0 + 0.4 * neonKnob(5));
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = foldFinish(foldProbe(ro, rd), 1.3);
}
