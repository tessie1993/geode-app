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

// Fluor Orb: one carved ball hung in the dark and turned about. Raised walls
// part its whole surface into cells, two kinds in turn, and a lower ring sits
// inside every cell, so the ball is netted in glowing outlines.
//
// Morph embosses the ball: the walls and rings stand further out. Worst case
// 90 steps, no noise.

vec2 fluorMap(vec3 p) {
    p.xz = fluorRot(uTime * FLUOR_RATE(60)) * p.xz;
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

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    gFluorStride = 0.6;
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.6, CAM_SPIN_UNIT, ro, rd);
    fragColor = fluorFinish(fluorTrace(ro, rd), 1.1);
}
