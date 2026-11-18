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

// Petal Scales: a flight down a tunnel of overlapping scales, the way the
// bracts of a cone or the outer leaves of a bud overlap, every scale a cupped
// sheet that leaves the wall, leans in toward the way and lies over the row
// ahead.
//
// Three formulas (Formula, lib_layers), each a different cladding:
//   0  rows of nine round the way, each row turned by the golden angle from
//      the one behind it
//   1  a string of cones: the same rows on a wall that swells and narrows
//      four times a period, so the way runs through cone after cone
//   2  a quincunx of thirteen narrow scales a row, each row turned half a
//      scale from the last, leaning back toward the eye instead of away
//
// Morph lifts the scales off the wall, from lying almost flat to standing out
// like a cone half open. Worst case 96 steps and 4 sheets, three rows to a
// point, no noise.

/** The scale of row `row` nearest the angle th, at radius r and z along the way. */
vec3 scaleSheet(float r, float th, float z, float row, float count, float turn, float base, float dir, float wide) {
    float cell = PETAL_TAU / count;
    // The rows' turns repeat every 32 rows (16 world units), so the world tiles along z.
    float a = th - mod(row, 32.0) * turn;
    a -= cell * floor(a / cell + 0.5);
    float s = dir * (z - 0.5 * row);
    float lean = mix(0.12, 0.65, gPetalForm);
    // The scale's middle: it leaves the wall at its base, leans in and curls, cupped across.
    float arc = a * r;
    float surf = base - lean * s - 0.15 * s * s + 0.5 * arc * arc;
    float f = (r - surf) / sqrt(1.0 + lean * lean);
    vec2 e = vec2(arc / wide, (s - 0.55) / 0.55);
    float o = (length(e) - 1.0) * min(wide, 0.55);
    return vec3(petalCut(f, o), length(e), 2.0 * arc / wide);
}

/** The wall's radius at a row's foot: plain, or swelling and narrowing four times a period. */
float scaleBase(float row, float swell) {
    return 2.0 - swell * 0.55 * (0.5 - 0.5 * cos(4.0 * (0.5 * row) * (PETAL_TAU / PETAL_PERIOD)));
}

vec3 scaleRows(vec3 p, float swell) {
    float r = length(p.xy);
    float th = petalAtan(p.xy);
    // A scale is 1.1 long and a row 0.5, so three rows reach any point.
    float row = floor(p.z / 0.5);
    vec3 m = scaleSheet(r, th, p.z, row, 9.0, PETAL_GOLD, scaleBase(row, swell), 1.0, 0.62);
    m = petalNear(m, scaleSheet(r, th, p.z, row - 1.0, 9.0, PETAL_GOLD, scaleBase(row - 1.0, swell), 1.0, 0.62));
    return petalNear(m, scaleSheet(r, th, p.z, row - 2.0, 9.0, PETAL_GOLD, scaleBase(row - 2.0, swell), 1.0, 0.62));
}

vec3 scaleQuincunx(vec3 p) {
    float r = length(p.xy);
    float th = petalAtan(p.xy);
    // Leaning back toward the eye: the rows that reach a point are this one and the two after it.
    float row = floor(p.z / 0.5) + 1.0;
    float quin = PETAL_TAU / 26.0;
    vec3 m = scaleSheet(r, th, p.z, row, 13.0, quin, 2.0, -1.0, 0.36);
    m = petalNear(m, scaleSheet(r, th, p.z, row + 1.0, 13.0, quin, 2.0, -1.0, 0.36));
    return petalNear(m, scaleSheet(r, th, p.z, row + 2.0, 13.0, quin, 2.0, -1.0, 0.36));
}

vec3 petalMap(vec3 p) {
    vec3 m = vec3(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * scaleRows(p, 0.0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * scaleRows(p, 1.0);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * scaleQuincunx(p);
    return m;
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.6;
    gPetalLamp = vec3(0.0, 0.0, 4.0);
    gPetalGap = 8.0;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = petalFinish(petalTrace(petalEye(ro, 0.5), rd), 0.9);
}
