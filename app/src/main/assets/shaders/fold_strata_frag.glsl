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

// Fold Strata: a flight through a bicomplex Julia set, a fractal in four
// dimensions, cut by planes across the way a quarter unit apart. Each plane
// is a cut at its own place in the third and fourth dimensions, which walk
// round a circle as the way goes on, so the flight is a walk through the
// four-dimensional body, one section at a time: outlined, filled faintly,
// laid over each other front to back. The bicomplex power splits into two
// complex powers on two planes, so the body is the product of two Julia
// sets, and each section is two of them slid across each other.
//
// Three formulas (Formula, lib_layers), each a different iteration rule:
//   0  the square: rounded bulbs and their dendrites
//   1  the cube: three-fold bulbs, the sections budding in threes
//   2  the burning square: both parts folded to plus before squaring, so
//      the sections grow keels and ragged spires
//
// The iteration count runs from 7 to 10 with Detail, k1 and k4, the last one
// mixed in by its share (lib_layers).
//
// Morph widens the circle the cuts walk, and the two sets slide further
// apart. Worst case 40 sections and 10 iterations (of each complex power),
// no noise.

/** A complex Julia set's distance estimate at z for c under `rule`, in z's units, negative inside; trap: how near the orbit came to the real axis. */
float strataJulia(vec2 z, vec2 c, int rule, out float trap) {
    float dz = 1.0;
    float m2 = dot(z, z);
    trap = 1.0e3;
    float n = layerIters(7.0, 10.0);
    for (int i = 0; i < 10; i++) {
        float a = layerShare(n, i);
        if (a <= 0.0) break;
        vec2 zn;
        float dzn;
        if (rule == 1) {
            zn = vec2(z.x * z.x * z.x - 3.0 * z.x * z.y * z.y, 3.0 * z.x * z.x * z.y - z.y * z.y * z.y) + c;
            dzn = 3.0 * m2 * dz;
        } else {
            vec2 b = rule == 2 ? abs(z) : z;
            zn = vec2(b.x * b.x - b.y * b.y, 2.0 * b.x * b.y) + c;
            dzn = 2.0 * sqrt(m2) * dz;
        }
        z = mix(z, zn, a);
        dz = min(mix(dz, dzn, a), 1.0e20);
        m2 = dot(z, z);
        if (i < 3) trap = min(trap, abs(z.y) / max(dz, 1.0e-6));
        if (m2 > 256.0) break;
    }
    float r = sqrt(m2);
    return clamp(0.5 * r * log(max(r, 1.0e-6)) / max(dz, 1.0e-6), -1.0, 4.0);
}

vec4 strataBody(vec3 p, int rule) {
    float a = p.z * (FOLD_TAU / FOLD_PERIOD) + uTime * FOLD_RATE(30);
    float rad = 0.15 + 0.25 * gFoldForm;
    // 0.8 of the fractal's units to a world unit.
    vec4 x = vec4(p.xy * 0.8, rad * cos(a), rad * sin(a));
    // The two planes the bicomplex power splits on.
    vec2 u = vec2(x.x + x.w, x.y - x.z);
    vec2 v = vec2(x.x - x.w, x.y + x.z);
    // Constants inside each rule's connected set, so the sets have an inside to fill.
    vec2 cu = vec2(-1.0, 0.0);
    vec2 cv = vec2(-0.12, 0.75);
    if (rule == 1) {
        cu = vec2(-0.08, 0.42);
        cv = vec2(0.36, 0.1);
    } else if (rule == 2) {
        cu = vec2(-0.82, -0.18);
        cv = vec2(-1.2, 0.05);
    }
    cu += 0.04 * vec2(cos(uTime * FOLD_RATE(80)), sin(uTime * FOLD_RATE(80)));
    float tu;
    float tv;
    float du = strataJulia(u, cu, rule, tu);
    float dv = strataJulia(v, cv, rule, tv);
    // u and v stretch four-space by root 2, and a point is outside the product when it is outside either.
    float k = 0.70710678 / 0.8;
    return vec4(max(du, dv) * k, 0.3 * length(x.xy) + 0.2 * sin(a) + 0.2 * float(rule), vec2(tu, tv) * k);
}

vec4 foldMap(vec3 p) {
    float slack = layerFlowWarp(p, gFoldFlow, gFoldChaos);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * strataBody(p, 0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * strataBody(p, 1);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * strataBody(p, 2);
    m.x *= slack;
    return m;
}

//#include lib_fold_march_slice

void main() {
    vec2 uv = view();
    foldSetup();
    gFoldAxial = 1.0;
    gFoldGap = 0.25;
    gFoldDense = 0.3;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = foldFinish(foldSlicePlanes(foldEye(ro), rd), 1.2);
}
