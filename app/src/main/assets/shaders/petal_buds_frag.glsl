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

// Petal Buds: a flight low along a valley of closed buds. The floor is one
// thin sheet curved up on either side of the way; in each cell of it stands a
// bud of three petals wrapped round each other, each a third of an egg a
// little wider than the one inside it, and lamps hang low along the way.
//
// Morph opens the buds: the petals' tips part and turn out. Worst case 96
// steps and 4 sheets, three petals to a point, no noise.

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

vec3 petalMap(vec3 p) {
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
    vec3 m = petalNear(budPetal(rr, hb, phi, 0.0, tall), budPetal(rr, hb, phi, 1.0, tall));
    m = petalNear(m, budPetal(rr, hb, phi, 2.0, tall));
    // The next cell's bud is at least 0.15 past this cell's wall, and no bud rises past 0.95.
    m = petalHold(m, max(0.8 - max(abs(q.x), abs(q.y)) + 0.15, hb - 0.95));
    return petalNear(m, ground);
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
