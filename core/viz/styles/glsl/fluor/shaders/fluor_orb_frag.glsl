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

// Fluor Orb: one carved ball hung in the dark and turned about. Raised walls
// part its whole surface into cells, two kinds in turn, and a lower ring sits
// inside every cell, so the ball is netted in glowing outlines.
//
// Morph embosses the ball: the walls and rings stand further out.
//
// Formula, three balls (lib_strata): 0 the netted cells, as they always
// were; 0.5 a carved die: the ball's six faces (a cube's, blown out round)
// each a Menger carpet, frames round squares within squares; 1 six
// Apollonian rosettes, circles nested in the gaps between circles. Each is
// gStrataIters folds deep. The far fractal follows: an Apollonian ball, a
// Menger cube, a kaleidoscope dust, all round the ball.
//
// Worst case 90 steps, at most 3 folds a step, no noise; then the layers.

/** The ball at p (already turned) carved as a die by fold `kind`: each face a carpet or a rosette. */
vec2 orbDie(vec3 p, float kind) {
    float l = max(length(p), 1.0e-3);
    float h = l - 1.5;
    vec3 a = abs(p);
    // The face the direction falls on, and the place on it, about a unit from middle to edge.
    vec2 uv = a.x >= a.y && a.x >= a.z ? p.yz / a.x : (a.y >= a.z ? p.zx / a.y : p.xy / a.z);
    float face = a.x >= a.y && a.x >= a.z ? 0.0 : (a.y >= a.z ? 1.0 : 2.0);
    // On the ball a unit of uv is about 1.2 units of surface near a face's middle.
    vec2 orn = kind < 1.5 ? fluorOrnament(uv * 1.2, h, 3.0, vec2(1.0), 0.0, 1.2, mix(0.05, 0.16, gFluorForm))
        : fluorOrnament(uv * 1.2, h, 2.0, vec2(0.0), 0.0, 0.6, mix(0.05, 0.16, gFluorForm));
    return vec2(fluorSmin(h, orn.x, 0.03), 0.3 * face + orn.y);
}

vec2 netted(vec3 p) {
    float l = max(length(p), 1.0e-3);
    vec3 n = p / l;
    float h = l - 1.5;
    float f = dot(cos(n * 5.0), vec3(1.0));
    // The cells' net is where f is 0; this turns f into a distance across the ball's surface.
    float across = l / (5.0 * max(length(sin(n * 5.0)), 0.5));
    float top = mix(0.05, 0.16, gFluorForm);
    float wall = fluorStrip(h, f * across, 0.035, top);
    float ring = fluorStrip(h, (abs(f) - 1.6) * across, 0.025, 0.5 * top);
    float d = min(wall, ring);
    return vec2(fluorSmin(h, d, 0.03), f > 0.0 ? 0.2 : 0.7);
}

vec2 fluorMap(vec3 p) {
    p.xz = fluorRot(uTime * FLUOR_RATE(60)) * p.xz;
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = netted(p);
    if (strataUses(1)) b = orbDie(p, 1.0);
    if (strataUses(2)) c = orbDie(p, 2.0);
    return strataBlend2(a, b, c);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    strataSetup(vec3(2.0, 3.0, 0.0), 1.0, 0.4, 0.8);
    gFluorStride = 0.6;
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.6, CAM_SPIN_UNIT, ro, rd);
    fragColor = fluorFinish(fluorTrace(ro, rd), 1.1);
}
