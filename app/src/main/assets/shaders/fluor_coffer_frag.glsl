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

// Fluor Coffer: a flight down a square gallery whose four faces are coffered:
// rows of sunk panels, each stepped down twice to a round boss, so every
// panel is a nest of glowing frames.
//
// Morph sinks the coffers deeper and swells the bosses. Worst case 90 steps,
// no noise.

vec2 fluorMap(vec3 p) {
    vec2 b = abs(p.xy);
    float h = 1.3 - max(b.x, b.y);
    // Across the nearest face, and along the way: four panels across, one a unit along.
    vec2 u = vec2(b.x > b.y ? p.y : p.x, p.z);
    vec2 cell = floor(u / vec2(0.65, 1.0));
    vec2 q = u - (cell + 0.5) * vec2(0.65, 1.0);
    float deep = mix(0.05, 0.14, gFluorForm);
    float outer = fluorBox(vec3(q, h), vec3(0.27, 0.42, 0.5 * deep));
    float inner = fluorBox(vec3(q, h), vec3(0.18, 0.33, deep));
    float d = max(h, -min(outer, inner));
    float paint = 0.381966 * mod(cell.x + 2.0 * cell.y, 8.0);
    if (inner < outer) paint += 0.5;
    float boss = length(vec3(q, h + deep)) - (0.06 + 0.05 * gFluorForm);
    if (boss < d) {
        d = boss;
        paint = 0.25;
    }
    return vec2(d, paint);
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
