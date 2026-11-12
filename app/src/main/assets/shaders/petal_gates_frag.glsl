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

// Petal Gates: a flight through open blossoms set across the way, one every
// two units: seven broad petals round an open heart the way runs through, a
// lamp in every heart, each blossom turned by the golden angle from the last
// and slowly turning against its neighbours.
//
// Morph cups the blossoms toward the eye, from flat discs to deep bowls.
// Worst case 96 steps and 4 sheets, two blossoms to a point, no noise.

/** Blossom n, across the way at z = 2 n. */
vec3 gate(vec3 p, float n) {
    // Counted 0..7, so the eight blossoms of the world's 16 repeat with it.
    float k = mod(n, 8.0);
    float turn = k * PETAL_GOLD + (2.0 * mod(k, 2.0) - 1.0) * uTime * PETAL_RATE(40);
    float r = length(p.xy);
    float seg = PETAL_TAU / 7.0;
    float b = petalAtan(p.xy) - turn;
    b -= seg * floor(b / seg + 0.5);
    float along = r * cos(b);
    float across = r * sin(b);
    // The petals' tips come toward the eye as the blossom cups.
    float cup = mix(0.04, 0.33, gPetalForm);
    float u = max(r - 0.45, 0.0);
    float f = (p.z - 2.0 * n + cup * u * u) / sqrt(1.0 + 4.0 * cup * cup * u * u);
    vec2 e = vec2((along - 1.3) / 0.85, across / 0.55);
    return vec3(petalCut(f, (length(e) - 1.0) * 0.55), length(e), 2.0 * across / 0.55);
}

vec3 petalMap(vec3 p) {
    float n = floor(p.z / 2.0);
    vec3 m = petalNear(gate(p, n), gate(p, n + 1.0));
    // The blossom after those reaches back no nearer than a unit short of it.
    return petalHold(m, 2.0 * n + 3.0 - p.z);
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.75;
    gPetalLamp = vec3(0.0);
    gPetalGap = 2.0;
    gPetalFall = 4.0;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = petalFinish(petalTrace(petalEye(ro, 0.25), rd), 0.9);
}
