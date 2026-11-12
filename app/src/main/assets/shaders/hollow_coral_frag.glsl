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
//#include lib_hollow

// Hollow Coral: a flight down a reef pipe. The wall is close-packed polyps in
// offset rows, each a mound with a mouth at its top, ringed like a target.
//
// Morph turns the reef inside out: the mounds sink and become pits. Worst
// case 90 steps, no noise.

vec2 hollowMap(vec3 p) {
    float r = length(p.xy);
    float th = atan(p.y, p.x) * 8.0 / HOLLOW_TAU;
    vec2 g = vec2(th, p.z * 1.0);
    float row = floor(g.y);
    g.x += 0.5 * mod(row, 2.0);
    vec2 c = fract(g) - 0.5;
    float bump = max(1.0 - 4.0 * dot(c, c), 0.0);
    float mound = bump * bump * (1.0 - 1.6 * smoothstep(0.75, 1.0, bump));
    float d = 1.6 - r - mix(0.42, -0.42, gHollowForm) * mound;
    return vec2(d * 0.45, 3.0 * bump + 0.25);
}

//#include lib_hollow_march

void main() {
    vec2 uv = view();
    hollowSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = hollowFinish(hollowTrace(hollowEye(ro), rd), 1.0);
}
