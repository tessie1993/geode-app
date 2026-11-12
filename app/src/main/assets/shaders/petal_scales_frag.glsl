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
// bracts of a cone or the outer leaves of a bud overlap: rows of nine round
// the way, each row turned by the golden angle from the one behind it, every
// scale a cupped sheet that leaves the wall, leans in toward the way and lies
// over the row ahead.
//
// Morph lifts the scales off the wall, from lying almost flat to standing out
// like a cone half open. Worst case 96 steps and 4 sheets, three rows to a
// point, no noise.

/** The scale of row `row` nearest the angle th, at radius r and z along the way. */
vec3 scaleSheet(float r, float th, float z, float row) {
    float cell = PETAL_TAU / 9.0;
    // The rows' turns repeat every 32 rows (16 world units), so the world tiles along z.
    float a = th - mod(row, 32.0) * PETAL_GOLD;
    a -= cell * floor(a / cell + 0.5);
    float s = z - 0.5 * row;
    float lean = mix(0.12, 0.65, gPetalForm);
    // The scale's middle: it leaves the wall at its base, leans in and curls, cupped across.
    float arc = a * r;
    float surf = 2.0 - lean * s - 0.15 * s * s + 0.5 * arc * arc;
    float f = (r - surf) / sqrt(1.0 + lean * lean);
    vec2 e = vec2(arc / 0.62, (s - 0.55) / 0.55);
    float o = (length(e) - 1.0) * 0.55;
    return vec3(petalCut(f, o), length(e), 2.0 * arc / 0.62);
}

vec3 petalMap(vec3 p) {
    float r = length(p.xy);
    float th = petalAtan(p.xy);
    // A scale is 1.1 long and a row 0.5, so three rows reach any point.
    float row = floor(p.z / 0.5);
    vec3 m = petalNear(scaleSheet(r, th, p.z, row), scaleSheet(r, th, p.z, row - 1.0));
    return petalNear(m, scaleSheet(r, th, p.z, row - 2.0));
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
