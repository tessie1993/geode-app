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

// Petal Garland: blossoms strung on waving ribbons, turned about, the lamp in
// the middle. Each blossom faces out: five cupped petals round an open heart,
// every blossom turned by the golden angle from its neighbour.
//
// Three formulas (Formula, lib_layers), each a different stringing:
//   0  a ring of ten blossoms tipped up and down in turn
//   1  an armillary: two such rings, one upright through the other, like the
//      bands of an old sky globe
//   2  a starburst: six blossoms on the axes of a cube about the lamp, three
//      ribbons round it, and eight more on its corners grown in by the
//      iteration (Detail, k1 TEXTURE, k4 BUILD)
//
// Morph cups the petals, from flat open stars to closed bowls. Worst case 96
// steps and 4 sheets, no noise.

/** A blossom in its own frame: q.x out from where it is strung (0 its heart's plane), y, z across; `k` its count. */
vec3 garlandBlossom(vec3 q, float k, float size) {
    q /= size;
    // Round the blossom's axis (x): five petals, the whole turned by the golden angle per blossom.
    float rb = length(q.yz);
    float pseg = PETAL_TAU / 5.0;
    float beta = petalAtan(q.yz) - k * PETAL_GOLD;
    beta -= pseg * floor(beta / pseg + 0.5);
    float along = rb * cos(beta);
    float across = rb * sin(beta);
    float cup = mix(0.1, 1.1, gPetalForm);
    float f = (q.x - cup * rb * rb + 0.05) / sqrt(1.0 + 4.0 * cup * cup * rb * rb);
    vec2 e = vec2((along - 0.4) / 0.34, across / 0.2);
    return vec3(petalCut(f, (length(e) - 1.0) * 0.2) * size, length(e), 2.0 * across / 0.2);
}

/** A ring of ten blossoms of radius 2.4 in the xz plane, with its ribbon. */
vec3 garlandRing(vec3 p, float hue) {
    float seg = PETAL_TAU / 10.0;
    float ang = petalAtan(p.xz);
    float i = floor(ang / seg + 0.5);
    float a = ang - i * seg;
    float ry = length(p.xz);
    // The blossom's frame: x out from the ring, y up, z along it.
    vec3 q = vec3(ry * cos(a) - 2.4, p.y, ry * sin(a));
    // Counted 0..9, so the blossom across the angle's wrap is one blossom.
    float k = mod(i, 10.0);
    q.xy = petalRot(0.4 * (2.0 * mod(k, 2.0) - 1.0)) * q.xy;
    vec3 m = garlandBlossom(q, k + hue, 1.0);
    // A neighbour's blossom lies past this one's side of the ring, and every blossom within 0.75 of the ring.
    float ring = length(vec2(ry - 2.4, p.y));
    m = petalHold(m, max((0.5 * seg - abs(a)) * ry, ring - 0.75));
    // The ribbon, rising and falling between the blossoms.
    float y = p.y + 0.12 * cos(10.0 * ang);
    vec3 band = vec3(petalCut(ry - 2.25, abs(y) - 0.07), abs(y) / 0.07, 0.0);
    return petalNear(m, band);
}

vec3 garlandArmillary(vec3 p) {
    vec3 m = garlandRing(p, 0.0);
    // The second ring upright, through the first's poles, its blossoms turned on by half a step.
    vec3 q = vec3(p.x, -p.z, p.y);
    q.xz = petalRot(0.3141593) * q.xz;
    return petalNear(m, garlandRing(q * 1.15, 3.0) / vec3(1.15, 1.0, 1.0));
}

vec3 garlandBurst(vec3 p) {
    vec3 a = abs(p);
    // The nearest of the six axis blossoms: its frame, x out along its axis, and its count.
    vec3 q;
    float k;
    if (a.x >= a.y && a.x >= a.z) {
        q = vec3(a.x, p.y, p.z);
        k = p.x < 0.0 ? 3.0 : 0.0;
    } else if (a.y >= a.z) {
        q = vec3(a.y, p.z, p.x);
        k = p.y < 0.0 ? 4.0 : 1.0;
    } else {
        q = vec3(a.z, p.x, p.y);
        k = p.z < 0.0 ? 5.0 : 2.0;
    }
    vec3 m = garlandBlossom(q - vec3(1.9, 0.0, 0.0), k, 1.0);
    // Each axis blossom keeps to its own wedge: the planes between the wedges lie at least that far.
    m = petalHold(m, 0.7071068 * (q.x - max(abs(q.y), abs(q.z))) + 0.35);
    // The corner blossoms, grown in by the iteration, facing out along the diagonals.
    float grow = clamp(layerIters(-0.2, 1.5), 0.05, 1.0);
    vec3 d = vec3(dot(a, vec3(0.5773503)) - 1.8, dot(a, vec3(0.7071068, -0.7071068, 0.0)), dot(a, vec3(0.4082483, 0.4082483, -0.8164966)));
    m = petalNear(m, garlandBlossom(d, 4.0, 0.75 * grow));
    // Three ribbons, one round each axis, the nearest of them.
    float lo = min(a.x, min(a.y, a.z));
    float rr = sqrt(max(dot(p, p) - lo * lo, 0.0));
    vec3 band = vec3(petalCut(rr - 1.6, lo - 0.06), lo / 0.06, 0.0);
    return petalNear(m, band);
}

vec3 petalMap(vec3 p) {
    p.xz = petalRot(uTime * PETAL_RATE(40)) * p.xz;
    vec3 m = vec3(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * garlandRing(p, 0.0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * garlandArmillary(p);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * garlandBurst(p);
    return m;
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.7;
    vec3 ro;
    vec3 rd;
    camAround(uv, 5.2, CAM_SPIN_UNIT, ro, rd);
    fragColor = petalFinish(petalTrace(ro, rd), 1.0);
}
