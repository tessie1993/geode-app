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
//#include lib_strata
//#include lib_fluor

// Fluor Coil: a flight down the middle of two carved bands wound about the
// way, the outer one against the inner, seen through each other's gaps. The
// face of each band toward the way carries a running scroll between two edge
// ridges; the bands turn slowly, so the scrolls stream past.
//
// Morph widens the bands toward each other's edges and embosses the scroll.
//
// Formula, three scrolls (lib_strata): 0 the running scroll, as it always
// was; 0.5 the band's face carved in Apollonian rings, circles nested in the
// gaps between circles, a cell of them to every short stretch of band; 1 a
// Menger frieze, squares within squares down the band. Each is gStrataIters
// folds deep. The far fractal follows: a kaleidoscope dust, an Apollonian
// ball, a Mandelbox, glimpsed through the bands' gaps.
//
// Worst case 90 steps, at most 3 folds a band a step, no noise; then the
// layers.

/** One band wound about the way at radius rc, one turn every two units, `wind` +1 or -1: its distance and paint. */
vec2 coilBand(vec3 p, float rc, float wind, float paint, float kind) {
    p.xy = fluorRot(wind * uTime * FLUOR_RATE(30)) * p.xy;
    float r = length(p.xy);
    float th = fluorAngle(p.xy) / FLUOR_TAU;
    // How far along the way from the middle of the nearest turn.
    float s = 0.5 * p.z - wind * th;
    float ds = (fract(s + 0.5) - 0.5) * 2.0;
    float w = mix(0.5, 0.68, gFluorForm);
    vec2 b = abs(vec2(r - rc, ds)) - vec2(0.08, w);
    float core = length(max(b, 0.0)) + min(max(b.x, b.y), 0.0);
    // The face toward the way, and the relief on it, kept to the band.
    float h = rc - 0.08 - r;
    float top = mix(0.05, 0.12, gFluorForm);
    // Nine waves to a turn; their slope along the face is 2.7 / rc.
    float wave = (ds - 0.3 * sin(FLUOR_TAU * 9.0 * th)) / sqrt(1.0 + 7.29 / (rc * rc));
    float scroll = max(fluorStrip(h, wave, 0.03, top), abs(ds) - w);
    if (kind > 0.5) {
        // The face as a frieze: a whole number of cells to a turn, each about 0.7 long.
        float n = floor(FLUOR_TAU * rc / 0.7 + 0.5);
        float a = th * n;
        vec2 u = vec2((a - floor(a + 0.5)) * FLUOR_TAU * rc / n, ds);
        vec2 orn = kind < 1.5 ? fluorOrnament(u, h, 2.0, vec2(0.0), 0.0, 0.32, top)
            : fluorOrnament(u, h, 3.0, vec2(1.0), 0.0, 0.5, top);
        scroll = max(orn.x, abs(ds) - w);
    }
    float edge = fluorStrip(h, abs(ds) - w + 0.06, 0.025, top);
    float relief = max(min(scroll, edge), r - rc);
    float tag = relief < core ? paint + (scroll < edge ? 0.25 : 0.0) : paint;
    return vec2(fluorSmin(core, relief, 0.03), tag);
}

vec2 coils(vec3 p, float kind) {
    vec2 a = coilBand(p, 1.45, 1.0, 0.0, kind);
    vec2 b = coilBand(p, 2.8, -1.0, 0.5, kind);
    return a.x < b.x ? a : b;
}

vec2 fluorMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = coils(p, 0.0);
    if (strataUses(1)) b = coils(p, 1.0);
    if (strataUses(2)) c = coils(p, 2.0);
    return strataBlend2(a, b, c);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    strataSetup(vec3(0.0, 2.0, 1.0), 1.0, 0.4, 0.7);
    gFluorStride = 0.65;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.1);
}
