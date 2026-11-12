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
// laid over each other front to back. The bicomplex square splits into two
// complex squares on two planes, so the body is the product of two Julia
// sets, and each section is two of them slid across each other.
//
// Morph widens the circle the cuts walk, and the two sets slide further
// apart. Worst case 40 sections and 10 iterations (of each complex square),
// no noise.

/** A complex Julia set's distance estimate at z for c, in z's units, negative inside; trap: how near the orbit came to the real axis. */
float strataJulia(vec2 z, vec2 c, out float trap) {
    float dz = 1.0;
    float m2 = dot(z, z);
    trap = 1.0e3;
    for (int i = 0; i < 10; i++) {
        dz = min(2.0 * sqrt(m2) * dz, 1.0e20);
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        m2 = dot(z, z);
        if (i < 3) trap = min(trap, abs(z.y) / max(dz, 1.0e-6));
        if (m2 > 256.0) break;
    }
    float r = sqrt(m2);
    return clamp(0.5 * r * log(max(r, 1.0e-6)) / max(dz, 1.0e-6), -1.0, 4.0);
}

vec4 foldMap(vec3 p) {
    float a = p.z * (FOLD_TAU / FOLD_PERIOD) + uTime * FOLD_RATE(30);
    float rad = 0.15 + 0.25 * gFoldForm;
    // 0.8 of the fractal's units to a world unit.
    vec4 x = vec4(p.xy * 0.8, rad * cos(a), rad * sin(a));
    // The two planes the bicomplex square splits on.
    vec2 u = vec2(x.x + x.w, x.y - x.z);
    vec2 v = vec2(x.x - x.w, x.y + x.z);
    float tu;
    float tv;
    // Both constants lie inside a bulb of the Mandelbrot set, so both sets have an inside to fill.
    float du = strataJulia(u, vec2(-1.0, 0.0) + 0.04 * vec2(cos(uTime * FOLD_RATE(80)), sin(uTime * FOLD_RATE(80))), tu);
    float dv = strataJulia(v, vec2(-0.12, 0.75), tv);
    // u and v stretch four-space by root 2, and a point is outside the product when it is outside either.
    float k = 0.70710678 / 0.8;
    return vec4(max(du, dv) * k, 0.3 * length(x.xy) + 0.2 * sin(a), vec2(tu, tv) * k);
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
