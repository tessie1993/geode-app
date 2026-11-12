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
//#include lib_petal

// Petal Bells: a nest of five bell cups hung one inside another, turned
// about. Each bell is the one outside it shrunk by the same share about the
// same top, so their mouths hang in tiers, each mouth flared and scalloped
// into five lobes and every bell turned by the golden angle from the one
// outside it; the lamp hangs inside the outer two.
//
// Morph flares the mouths and lengthens the bells. Worst case 96 steps and 4
// sheets, three bells to a point, no noise.

/** Bell k of the nest (0 the outermost), hung from its top at the origin, opening down. */
vec3 bell(vec3 p, float k) {
    // 0.72 to the k.
    float s = exp(-0.3285 * k);
    vec3 q = p / s;
    float h = max(-q.y, 0.0);
    float phi = petalAtan(q.xz) - k * PETAL_GOLD;
    float mouth = mix(1.4, 1.9, gPetalForm) * (1.0 - 0.07 * (1.0 - cos(5.0 * phi)));
    float flare = smoothstep(0.55 * mouth, mouth, h);
    // Pointed at the top, widening down to the flared lip.
    float rad = 0.75 * h / sqrt(h + 0.2) + mix(0.15, 0.6, gPetalForm) * flare * flare;
    float o = max(h - mouth, q.y);
    return vec3(petalCut((length(q.xz) - rad) * 0.7 * s, o * s), h / mouth, 10.0 * phi / PETAL_TAU);
}

vec3 petalMap(vec3 p) {
    p.xz = petalRot(uTime * PETAL_RATE(55)) * p.xz;
    p.y -= 1.0;
    // Which bells lie about this distance from the top: each is 0.72 the one outside it.
    float k = clamp(floor(log(max(length(p), 1.0e-4) / 1.3) / -0.3285), 0.0, 4.0);
    vec3 m = petalNear(bell(p, k), bell(p, min(k + 1.0, 4.0)));
    return petalNear(m, bell(p, max(k - 1.0, 0.0)));
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.6;
    gPetalLamp = vec3(0.0, -0.1, 0.0);
    gPetalFall = 3.0;
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.4, CAM_SPIN_UNIT, ro, rd);
    fragColor = petalFinish(petalTrace(ro, rd), 1.0);
}
