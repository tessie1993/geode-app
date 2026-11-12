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

// Fluor Weave: a flight down a cleft between two tall walls carved as
// basketwork, strips running along the way and strips running up, each
// passing over one and under the next, so every crossing shows a glowing
// outline lying across another.
//
// Morph works the weave: the strips rise and dip further over and under.
// Worst case 90 steps, no noise.

vec2 fluorMap(vec3 p) {
    // The right wall's weave sits a quarter strip higher than the left's.
    float right = step(0.0, p.x);
    float h = 1.3 - abs(p.x);
    // In strips: one every half unit, along the way and up the wall.
    vec2 u = 2.0 * vec2(p.z, p.y + 0.25 * right);
    vec2 id = floor(u + 0.5);
    vec2 g = 0.5 * (u - id);
    float dip = mix(0.015, 0.06, gFluorForm);
    float rowTop = 0.08 + dip * cos(3.1415927 * (u.x + id.y));
    float colTop = 0.08 - dip * cos(3.1415927 * (u.y + id.x));
    float row = fluorStrip(h, g.y, 0.2, rowTop);
    float col = fluorStrip(h, g.x, 0.2, colTop);
    float d = row;
    float paint = 0.381966 * mod(id.y, 5.0);
    if (col < d) {
        d = col;
        paint = 0.5 + 0.381966 * mod(id.x, 4.0);
    }
    return vec2(fluorSmin(h, d, 0.02), paint);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    gFluorStride = 0.65;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.0);
}
