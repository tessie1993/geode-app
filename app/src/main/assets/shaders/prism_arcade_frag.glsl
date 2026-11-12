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
//#include lib_prism

// Prism Arcade: a flight down a corridor of arches in a hall of mirrors. Two
// chrome walls, an arch cut through them every two units, stand on a chrome
// floor; across the way the corridor repeats, each one the mirror image of the
// next, so every arch opens on another arcade and another beyond it. A line of
// light runs round every arch, along the walls' tops and down the middle of
// each corridor's floor.
//
// Morph draws the arches up from round to pointed. Worst case 108 steps, no
// noise.

vec2 prismMap(vec3 p) {
    float xr = abs(p.x - 2.6 * floor(p.x / 2.6 + 0.5));
    float wall = 1.22 - xr;
    float zz = p.z - 2.0 * floor(p.z * 0.5 + 0.5);
    float a = 0.62;
    float o = 0.6 * a * gPrismForm;
    vec2 u = vec2(abs(zz), p.y - 0.1);
    float open = u.y > 0.0 ? length(u + vec2(o, 0.0)) - (a + o) : u.x - a;
    float d = max(max(wall, -open), p.y - 1.9);
    d = min(d, p.y + 1.1);
    // The arches' line keeps to the walls; the floor carries only its middle line.
    float seam = min(min(abs(open) + max(1.0 - xr, 0.0), abs(p.y - 1.9)), xr);
    return vec2(d, seam);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.0);
}
