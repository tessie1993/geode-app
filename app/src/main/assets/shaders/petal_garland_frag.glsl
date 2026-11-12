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

// Petal Garland: a ring of ten blossoms strung on a waving ribbon, turned
// about, the lamp in the ring's middle. Each blossom faces out from the ring,
// tipped up and down in turn: five cupped petals round an open heart, every
// blossom turned by the golden angle from its neighbour.
//
// Morph cups the petals, from flat open stars to closed bowls. Worst case 96
// steps and 4 sheets, no noise.

vec3 petalMap(vec3 p) {
    p.xz = petalRot(uTime * PETAL_RATE(40)) * p.xz;
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
    vec3 m = vec3(petalCut(f, (length(e) - 1.0) * 0.2), length(e), 2.0 * across / 0.2);
    // A neighbour's blossom lies past this one's side of the ring, and every blossom within 0.75 of the ring.
    float ring = length(vec2(ry - 2.4, p.y));
    m = petalHold(m, max((0.5 * seg - abs(a)) * ry, ring - 0.75));
    // The ribbon, rising and falling between the blossoms.
    float y = p.y + 0.12 * cos(10.0 * ang);
    vec3 band = vec3(petalCut(ry - 2.25, abs(y) - 0.07), abs(y) / 0.07, 0.0);
    return petalNear(m, band);
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
