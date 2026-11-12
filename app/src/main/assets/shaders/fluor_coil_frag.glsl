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
//#include lib_fluor

// Fluor Coil: a flight down the middle of two carved bands wound about the
// way, the outer one against the inner, seen through each other's gaps. The
// face of each band toward the way carries a running scroll between two edge
// ridges; the bands turn slowly, so the scrolls stream past.
//
// Morph widens the bands toward each other's edges and embosses the scroll.
// Worst case 90 steps, no noise.

/** One band wound about the way at radius rc, one turn every two units, `wind` +1 or -1: its distance and paint. */
vec2 coilBand(vec3 p, float rc, float wind, float paint) {
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
    float edge = fluorStrip(h, abs(ds) - w + 0.06, 0.025, top);
    float relief = max(min(scroll, edge), r - rc);
    float tag = relief < core ? paint + (scroll < edge ? 0.25 : 0.0) : paint;
    return vec2(fluorSmin(core, relief, 0.03), tag);
}

vec2 fluorMap(vec3 p) {
    vec2 a = coilBand(p, 1.45, 1.0, 0.0);
    vec2 b = coilBand(p, 2.8, -1.0, 0.5);
    return a.x < b.x ? a : b;
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    gFluorStride = 0.65;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.1);
}
