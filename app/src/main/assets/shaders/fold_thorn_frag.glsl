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

// Fold Thorn: coordinates folded to plus, then raised to a power and the
// start added: the plane's burning-ship map carried into space, so keels and
// thorns grow along every fold. The power is not conformal, so the usual
// distance estimate is no bound; the body is the level of the escape
// potential instead, and the probe march finds it by its change of sign.
// Turned about.
//
// Three formulas (Formula, lib_layers), each a different iteration rule:
//   0  every coordinate folded, then squared the quaternion way: thorns
//      along all three folds
//   1  one coordinate folded only: keels along one plane, the body
//      smooth and swollen on the others, like a hull with a spined ridge
//   2  every coordinate folded, then cubed the spherical way (a burning
//      bulb of power three): thorns in threes about rounded lobes
//
// The iteration count runs from 5 to 7 with Detail, k1 and k4, the last one
// mixed in by its share (lib_layers).
//
// Morph draws what is added toward one fixed point, from the ship toward its
// Julia relatives, and the thorns lengthen and twist. Worst case 52 strides
// and 10 more for the hit, 7 iterations, no noise.

vec4 thornBody(vec3 p, int rule) {
    // 1.15 of the fractal's units to a world unit, the body's middle at the origin.
    vec3 c = p * 1.15 + (rule == 2 ? vec3(-0.2, 0.0, 0.0) : vec3(-0.45, -0.35, -0.35));
    vec3 add = mix(c, vec3(-1.05, -0.3, -0.25), 0.35 * gFoldForm);
    vec3 z = c;
    float m2 = dot(z, z);
    float md = 1.0;
    // The power to the minus the number of powers so far.
    float fall = 1.0;
    float least = m2;
    vec2 trap = vec2(1.0e3);
    float n = layerIters(5.0, 7.0);
    for (int i = 0; i < 7; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0) break;
        vec3 zn;
        float mdn;
        float drop = 0.5;
        if (rule == 2) {
            vec3 b = abs(z);
            float r = sqrt(m2);
            float th = 3.0 * asin(clamp(b.z / max(r, 1.0e-6), -1.0, 1.0));
            float ph = 3.0 * atan(b.y, b.x + 1.0e-6);
            zn = m2 * r * vec3(cos(th) * cos(ph), cos(th) * sin(ph), sin(th)) + add;
            mdn = 3.0 * m2 * md + 1.0;
            drop = 0.33333333;
        } else {
            vec3 b = rule == 1 ? vec3(z.x, abs(z.y), z.z) : abs(z);
            zn = vec3(b.x * b.x - b.y * b.y - b.z * b.z, 2.0 * b.x * b.y, 2.0 * b.x * b.z) + add;
            mdn = 2.0 * sqrt(m2) * md + 1.0;
        }
        z = mix(z, zn, a);
        md = min(mix(md, mdn, a), 1.0e20);
        fall = mix(fall, drop * fall, a);
        m2 = dot(z, z);
        least = min(least, m2);
        if (i < 3) trap = min(trap, vec2(abs(z.y), abs(z.z)) / (1.15 * md));
        if (m2 > 1.0e4) break;
    }
    // The escape potential; the body is where it is under 0.05. Over its rough gradient (the derivative's
    // length, not a bound), so the field is about a world unit per unit near the body.
    float g = 0.5 * log(max(m2, 1.0)) * fall;
    float grad = 1.15 * fall * md / sqrt(max(m2, 1.0));
    float f = clamp((g - 0.05) / max(grad, 1.0e-3), -1.0, 1.0);
    return vec4(f, 0.5 * sqrt(least) + 0.2 * float(rule), trap);
}

vec4 foldMap(vec3 p) {
    p.xz = foldRot(uTime * FOLD_RATE(55)) * p.xz;
    float slack = layerFlowWarp(p, gFoldFlow, gFoldChaos);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * thornBody(p, 0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * thornBody(p, 1);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * thornBody(p, 2);
    m.x *= slack;
    return m;
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
