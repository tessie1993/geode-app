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
// two units, round an open heart the way runs through, a lamp in every heart,
// each blossom turned by the golden angle from the last and slowly turning
// against its neighbours.
//
// Three formulas (Formula, lib_layers), each a different gate:
//   0  seven broad petals round the heart
//   1  an iris: nine long blades, each tilted about its own length so it
//      lies over the next like the leaves of a shutter
//   2  rings: three rings of petals one outside another, seven small, seven
//      broad and eleven wide, the outermost grown in by the iteration
//      (Detail, k1 TEXTURE, k4 BUILD)
//
// Morph cups the blossoms toward the eye, from flat discs to deep bowls.
// Worst case 96 steps and 4 sheets, two blossoms to a point, no noise.

/** The turn of blossom n: the golden angle per blossom and a slow turn against its neighbours. */
float gateTurn(float n) {
    // Counted 0..7, so the eight blossoms of the world's 16 repeat with it.
    float k = mod(n, 8.0);
    return k * PETAL_GOLD + (2.0 * mod(k, 2.0) - 1.0) * uTime * PETAL_RATE(40);
}

/** A ring of `count` petals of blossom n, centred `mid` out, `len` and `wide` their half sizes, `tilt` each blade's lean. */
vec3 gateRing(vec3 p, float n, float count, float mid, float len, float wide, float tilt, float turn) {
    float r = length(p.xy);
    float seg = PETAL_TAU / count;
    float b = petalAtan(p.xy) - turn;
    b -= seg * floor(b / seg + 0.5);
    float along = r * cos(b);
    float across = r * sin(b);
    // The petals' tips come toward the eye as the blossom cups.
    float cup = mix(0.04, 0.33, gPetalForm);
    float u = max(r - 0.45, 0.0);
    float f = (p.z - 2.0 * n + cup * u * u - tilt * across) / sqrt(1.0 + 4.0 * cup * cup * u * u + tilt * tilt);
    vec2 e = vec2((along - mid) / len, across / wide);
    return vec3(petalCut(f, (length(e) - 1.0) * wide), length(e), 2.0 * across / wide);
}

vec3 gateBroad(vec3 p, float n) {
    return gateRing(p, n, 7.0, 1.3, 0.85, 0.55, 0.0, gateTurn(n));
}

vec3 gateIris(vec3 p, float n) {
    // The blades' inner tips leave the heart open (0.45 to 0.9 across, wider as Morph cups them).
    float open = mix(1.35, 1.8, gPetalForm);
    return gateRing(p, n, 9.0, open, 0.9, 0.42, 0.55, gateTurn(n));
}

vec3 gateRings(vec3 p, float n) {
    float r = length(p.xy);
    float turn = gateTurn(n);
    // One ring to a band of radius; across a band's edge the next ring lies at least that far.
    if (r < 1.0) return petalHold(gateRing(p, n, 7.0, 0.7, 0.3, 0.2, 0.0, turn + 0.4488), 1.05 - r);
    if (r < 2.0) return petalHold(gateRing(p, n, 7.0, 1.5, 0.5, 0.4, 0.0, turn), min(r - 0.95, 2.05 - r));
    float grow = clamp(layerIters(-0.2, 1.5), 0.05, 1.0);
    return petalHold(gateRing(p, n, 11.0, 2.0 + 0.6 * grow, 0.6 * grow, 0.33 * grow, 0.0, -turn), r - 1.95);
}

vec3 gateAt(vec3 p, float n) {
    vec3 m = vec3(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * gateBroad(p, n);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * gateIris(p, n);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * gateRings(p, n);
    return m;
}

vec3 petalMap(vec3 p) {
    float n = floor(p.z / 2.0);
    vec3 m = petalNear(gateAt(p, n), gateAt(p, n + 1.0));
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
