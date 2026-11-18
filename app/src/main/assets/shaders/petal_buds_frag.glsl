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

// Petal Buds: a flight low among buds, a bud to every cell of a grid, lamps
// hung low along the way.
//
// Three formulas (Formula, lib_layers), each a different planting:
//   0  a valley: one thin sheet curved up on either side of the way, in each
//      cell a bud of three petals wrapped round each other, each a third of
//      an egg a little wider than the one inside it
//   1  lotus: the same valley, in each cell an open cup of two rings of six
//      petals, the inner ring turned half a petal from the outer
//   2  a burrow: the buds grow inward from the wall of a tube round the way,
//      nine to a ring, so they close over the way from every side
//
// Morph opens the buds: the petals' tips part and turn out (the lotus cups
// flatten). Worst case 96 steps and 4 sheets, three petals to a point, no
// noise.

/** Petal j of a bud `tall` high, at radius rr from its axis, height h above its foot and angle phi round it. */
vec3 budPetal(float rr, float h, float phi, float j, float tall) {
    float a = phi - j * PETAL_TAU / 3.0;
    a -= PETAL_TAU * floor(a / PETAL_TAU + 0.5);
    float hc = clamp(h / tall, 0.0, 1.0);
    // An egg, closed at both ends, its top turned out as it opens; each petal wraps the one inside it.
    float rad = (0.65 * sqrt(hc * (1.0 - hc) + 0.002) - 0.03 + 0.3 * gPetalForm * hc * hc * hc) * tall * (1.0 + 0.08 * j);
    // Each petal wraps 155 degrees of the bud, so the three overlap.
    float o = max((abs(a) - 1.35) * rr, max(-h, h - tall));
    return vec3(petalCut((rr - rad) * 0.7, o), max(abs(a) / 1.35, hc), 3.0 * a / 1.35);
}

/** A bud of three petals. */
vec3 budThree(float rr, float h, float phi, float tall) {
    vec3 m = petalNear(budPetal(rr, h, phi, 0.0, tall), budPetal(rr, h, phi, 1.0, tall));
    return petalNear(m, budPetal(rr, h, phi, 2.0, tall));
}

/** One ring of six cupped petals of a lotus `tall` high, `turn` its turn about the axis. */
vec3 budRing(float rr, float h, float phi, float tall, float reach, float turn) {
    float seg = PETAL_TAU / 6.0;
    float b = phi - turn;
    b -= seg * floor(b / seg + 0.5);
    float along = rr * cos(b);
    float across = rr * sin(b);
    float cup = mix(1.0, 0.3, gPetalForm);
    float f = (h - 0.08 * tall - cup * rr * rr) / sqrt(1.0 + 4.0 * cup * cup * rr * rr);
    vec2 e = vec2((along - 0.5 * reach) / (0.5 * reach), across / (0.26 * reach));
    return vec3(petalCut(f, (length(e) - 1.0) * 0.26 * reach), length(e), 2.0 * across / (0.26 * reach));
}

vec3 budLotus(float rr, float h, float phi, float tall) {
    return petalNear(budRing(rr, h, phi, tall, 0.72, 0.0), budRing(rr, h + 0.06, phi, tall, 0.5, PETAL_TAU / 12.0));
}

vec3 budValley(vec3 p, bool lotus) {
    // The valley floor: low along the way, rising either side.
    vec3 ground = vec3((p.y + 1.3 - 0.16 * p.x * p.x) / sqrt(1.0 + 0.1024 * p.x * p.x), 0.0, 0.0);
    vec2 c = floor(p.xz / 1.6);
    vec2 q = p.xz - 1.6 * c - 0.8;
    // Ten cells to the world's 16 along z.
    float h = petalHash(vec2(c.x, mod(c.y, 10.0)));
    float tall = 0.6 + 0.35 * h;
    float cx = 1.6 * c.x + 0.8;
    float hb = p.y + 1.3 - 0.16 * cx * cx;
    float rr = length(q);
    float phi = petalAtan(q) + PETAL_TAU * h;
    vec3 m = lotus ? budLotus(rr, hb, phi, tall) : budThree(rr, hb, phi, tall);
    // The next cell's bud is at least 0.15 past this cell's wall, and no bud rises past 0.95.
    m = petalHold(m, max(0.8 - max(abs(q.x), abs(q.y)) + 0.15, hb - 0.95));
    return petalNear(m, ground);
}

vec3 budBurrow(vec3 p) {
    float r = length(p.xy);
    float th = petalAtan(p.xy);
    // Nine buds round a ring of the wall, ten rings to the world's 16.
    float cw = PETAL_TAU / 9.0;
    float ci = floor(th / cw + 0.5);
    float zc = floor(p.z / 1.6);
    // The bud's frame on the wall: across it round the way, along it down the way, up from the wall inward.
    vec2 q = vec2((th - ci * cw) * 2.2, p.z - 1.6 * zc - 0.8);
    float hb = 2.2 - r;
    float h = petalHash(vec2(mod(ci, 9.0) + 3.0, mod(zc, 10.0)));
    float tall = 0.55 + 0.35 * h;
    float rr = length(q);
    vec3 m = budThree(rr, hb, petalAtan(q) + PETAL_TAU * h, tall);
    m = petalHold(m, max(0.76 - max(abs(q.x), abs(q.y)) + 0.15, hb - 0.95));
    // Round the wall, a step across is shorter than the frame says inside the wall's radius.
    m.x *= 0.85;
    return petalNear(m, vec3(hb, 0.0, 0.0));
}

vec3 petalMap(vec3 p) {
    vec3 m = vec3(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * budValley(p, false);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * budValley(p, true);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * budBurrow(p);
    return m;
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.7;
    gPetalLamp = vec3(0.0, -0.2, 4.0);
    gPetalGap = 8.0;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = petalFinish(petalTrace(petalEye(ro, 0.3), rd), 1.0);
}
