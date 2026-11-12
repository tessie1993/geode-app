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

// Fluor Whorl: the family's opener. A flight between a carved floor and
// ceiling laid out in square tiles, each framed by a ridge and filled with one
// raised spiral round a boss, the next tile's spiral wound the other way; the
// spirals turn slowly, so their arms run outward.
//
// Morph embosses the tiles: the spirals stand higher and the bosses swell.
// Worst case 90 steps, no noise.

vec2 fluorMap(vec3 p) {
    // The ceiling's tiles sit half a tile over from the floor's.
    float up = step(0.0, p.y);
    float h = 1.1 - abs(p.y);
    vec2 u = p.xz + 0.5 * up;
    vec2 cell = floor(u);
    vec2 q = u - cell - 0.5;
    float r = length(q);
    float side = 2.0 * mod(cell.x + cell.y, 2.0) - 1.0;
    float s = r / 0.11 - side * (fluorAngle(q) / FLUOR_TAU + uTime * FLUOR_RATE(40) / FLUOR_TAU);
    float top = mix(0.04, 0.11, gFluorForm);
    float spiral = max(fluorStrip(h, (fract(s) - 0.5) * 0.11, 0.022, top), r - 0.4);
    float boss = length(vec3(q, h)) - (0.05 + 0.06 * gFluorForm);
    float frame = fluorStrip(h, 0.5 - max(abs(q.x), abs(q.y)), 0.03, 0.6 * top + 0.02);
    float d = min(spiral, boss);
    float paint = 0.381966 * mod(cell.x + 3.0 * cell.y, 8.0);
    if (frame < d) {
        d = frame;
        paint = 0.5;
    }
    return vec2(fluorSmin(h, d, 0.04), paint);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.0);
}
