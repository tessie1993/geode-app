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

// Petal Drift: a flight through a slow fall of loose petals, each its own size
// and turn, tumbling on its own clock; every petal a curved sheet with veins
// fanning from its foot.
//
// Three formulas (Formula, lib_layers), each a different fall:
//   0  one petal to a cell of a lattice
//   1  a spiral fall: six helices of petals wound round the way, two turns
//      in every period, a petal every two units along each
//   2  blossoms: one to a cell of the lattice, each five petals round a
//      heart, tumbling whole
//
// Morph curls the petals, from flat sheets to deep cups. Worst case 96 steps
// and 4 sheets, no noise.

/** One petal at q0 from its cell's middle, `h` its hash; tumbling (by `tumble`, 0 or 1), sized and curled. */
vec3 driftPetal(vec3 q0, float h, float shrink, float tumble) {
    float h2 = fract(h * 57.31);
    float h3 = fract(h * 13.7);
    vec3 q = q0 - 0.15 * (vec3(h, h2, h3) - 0.5);
    // Each petal tumbles on its own clock, a whole number of turns per wrap.
    q.xy = petalRot(tumble * (PETAL_TAU * h + uTime * PETAL_RATE(90.0 + 40.0 * floor(3.0 * h2)))) * q.xy;
    q.yz = petalRot(tumble * (PETAL_TAU * h3 + uTime * PETAL_RATE(70.0 + 30.0 * floor(3.0 * h)))) * q.yz;
    float size = (0.8 + 0.3 * h2) * shrink;
    q /= size;
    // A piece of a ball through the petal's middle, curved toward +z, cut to an oval and to the ball's near half.
    float cr = 1.0 / mix(0.8, 3.0, gPetalForm);
    float f = length(q - vec3(0.0, 0.0, cr)) - cr;
    vec2 e = vec2(q.x / 0.28, (q.y - 0.08) / 0.5);
    float o = max((length(e) - 1.0) * 0.28, q.z - cr);
    // Veins fan out from the petal's foot.
    float vein = 30.0 * petalAtan(vec2(q.y + 0.42, q.x)) / PETAL_TAU;
    return vec3(petalCut(f, o) * size, length(e), vein);
}

vec3 driftLattice(vec3 p) {
    vec3 c = floor(p * 0.5);
    vec3 q0 = p - 2.0 * c - 1.0;
    float h = petalHash(c.xy + 17.0 * mod(c.z, 8.0));
    // A petal stays inside its cell; a neighbour's is at least a fifth past the wall.
    return petalHold(driftPetal(q0, h, 1.0, 1.0), 1.0 - max(max(abs(q0.x), abs(q0.y)), abs(q0.z)) + 0.2);
}

vec3 driftSpiral(vec3 p) {
    // Two turns round the way in a period, so the world still repeats; the twist's shear bounds the step.
    vec2 c = petalRot(2.0 * p.z * (PETAL_TAU / PETAL_PERIOD)) * p.xy;
    vec2 s = layerSector(c, 6.0) - vec2(1.9, 0.0);
    float k = floor(p.z * 0.5);
    vec3 q0 = vec3(s, p.z - 2.0 * k - 1.0);
    float h = petalHash(vec2(mod(k, 8.0), 5.0));
    vec3 m = petalHold(driftPetal(q0, h, 1.0, 1.0), 1.0 - max(max(abs(q0.x), abs(q0.y)), abs(q0.z)) + 0.2);
    m.x *= 0.6;
    return m;
}

vec3 driftBlossom(vec3 p) {
    vec3 c = floor(p * 0.5);
    vec3 q0 = p - 2.0 * c - 1.0;
    float h = petalHash(c.xy + 17.0 * mod(c.z, 8.0));
    float h3 = fract(h * 13.7);
    // The blossom tumbles whole; its five petals are one folded about its heart's axis.
    vec3 q = q0;
    q.xz = petalRot(PETAL_TAU * h + uTime * PETAL_RATE(60.0 + 30.0 * floor(3.0 * h3))) * q.xz;
    q.yz = petalRot(PETAL_TAU * h3 + uTime * PETAL_RATE(50)) * q.yz;
    vec2 b = layerSector(q.xy, 5.0);
    // The petal's foot at the heart, its length out along the sector's middle.
    vec3 m = driftPetal(vec3(b.y, b.x - 0.3, q.z), 0.5, 0.65, 0.0);
    m.z += 7.0 * h;
    return petalHold(m, 1.0 - max(max(abs(q0.x), abs(q0.y)), abs(q0.z)) + 0.2);
}

vec3 petalMap(vec3 p) {
    vec3 m = vec3(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * driftLattice(p);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * driftSpiral(p);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * driftBlossom(p);
    return m;
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.7;
    gPetalLamp = vec3(0.0, 0.0, 4.0);
    gPetalGap = 8.0;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = petalFinish(petalTrace(petalEye(ro, 1.0), rd), 1.1);
}
