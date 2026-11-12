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
// mirrored cell two wide. In each, space is folded, then squared the
// quaternion way with its two side axes turned over, and a fixed point added:
// a Julia set of a square that is not conformal, so it has no distance to
// trust. The crystal is the level of the escape potential, found by the probe
// march; the way ahead is kept clear.
//
// Three formulas (Formula, lib_layers), each a different fold before the
// square:
//   0  a box fold: blocky crystals with sharp terraces
//   1  a ball fold: space near the middle turned inside out first, so the
//      crystals grow round blisters and geodes
//   2  the cube's nine mirrors: every crystal folded to the cube's symmetry,
//      faceted like a cut stone
//
// The iteration count runs from 5 to 7 with Detail, k1 and k4, the last one
// mixed in by its share (lib_layers).
//
// Morph moves the fixed point, and the crystals grow new faces and lose old
// ones. Worst case 52 strides and 10 more for the hit, 7 iterations, no noise.

vec4 quartzBody(vec3 p, int rule) {
    // 1.4 of the fractal's units to a world unit.
    vec3 z = foldRepeat(p, 2.0) * 1.4;
    vec3 c = vec3(-0.55, 0.42, -0.3) + gFoldForm * vec3(0.12, -0.1, 0.08);
    if (rule == 2) c = vec3(-0.62, 0.3, 0.22) + gFoldForm * vec3(0.1, 0.08, -0.1);
    float m2 = dot(z, z);
    float md = 1.4;
    // 2 to the minus the number of squares so far.
    float fall = 1.0;
    float least = m2;
    vec2 trap = vec2(1.0e3);
    float n = layerIters(5.0, 7.0);
    for (int i = 0; i < 7; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0) break;
        vec3 f = z;
        float k = 1.0;
        if (rule == 1) {
            k = clamp(0.5 / max(dot(z, z), 1.0e-6), 1.0, 2.2);
            f = z * k;
        } else if (rule == 2) {
            f = foldOcta(z);
        } else {
            f = foldBox(z, 0.85);
        }
        float mdn = min(2.0 * length(f) * md * k, 1.0e20);
        vec3 zn = vec3(f.x * f.x - f.y * f.y - f.z * f.z, -2.0 * f.x * f.y, -2.0 * f.x * f.z) + c;
        z = mix(z, zn, a);
        md = mix(md, mdn, a);
        fall = mix(fall, 0.5 * fall, a);
        m2 = dot(z, z);
        least = min(least, m2);
        if (i < 3) trap = min(trap, vec2(abs(z.x), abs(z.y)) / max(md, 1.0e-6));
        if (m2 > 1.0e4) break;
    }
    // The escape potential; the crystal is where it is under 0.05. Over its rough gradient (the
    // derivative's length, not a bound), so the field is about a world unit per unit near the crystal.
    float g = 0.5 * log(max(m2, 1.0)) * fall;
    float grad = fall * md / sqrt(max(m2, 1.0));
    float d = clamp((g - 0.05) / max(grad, 1.0e-3), -1.0, 1.0);
    d = max(d, 0.75 - length(p.xy));
    return vec4(d, 0.5 * sqrt(least) + 0.15 * sin(p.z * (FOLD_TAU / FOLD_PERIOD)) + 0.2 * float(rule), trap);
}

vec4 foldMap(vec3 p) {
    float slack = layerFlowWarp(p, gFoldFlow, gFoldChaos);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * quartzBody(p, 0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * quartzBody(p, 1);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * quartzBody(p, 2);
    m.x *= slack;
    return m;
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
