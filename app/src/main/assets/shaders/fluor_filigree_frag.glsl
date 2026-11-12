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

// Fluor Filigree: a flight between stacked sheets of lace, three above and
// three below. Each sheet is fine wire bent into a net of rounded loops, a
// thicker boss in every loop and open between, and each bends its own way, so
// the sheets beyond show through as layer under layer of glowing lace.
//
// Morph thickens the wire and the bosses. Worst case 90 steps, no noise.

vec2 fluorMap(vec3 p) {
    float ay = abs(p.y);
    float j = clamp(floor((ay - 0.7) / 0.8 + 0.5), 0.0, 2.0);
    float y = ay - 0.7 - 0.8 * j;
    float sheet = j + 3.0 * step(0.0, p.y);
    // The lace: a field whose zero lines bend into a net of loops, two to a unit along the way.
    vec2 u = FLUOR_TAU * p.xz;
    float a = u.x + 0.8 * sin(u.y) + 1.3 * sheet;
    float b = u.y + 0.8 * sin(u.x + 2.1 * sheet);
    float f = sin(a) * sin(b);
    vec2 grad = vec2(cos(a) * sin(b) + 0.8 * sin(a) * cos(b) * cos(u.x + 2.1 * sheet),
                     0.8 * cos(a) * sin(b) * cos(u.y) + sin(a) * cos(b));
    // Held under a quarter unit, so the middle of a loop never reads further from its wire than it is.
    float across = 1.0 / (FLUOR_TAU * max(length(grad), 0.6));
    float wire = fluorSlab(abs(f) * across - mix(0.012, 0.035, gFluorForm), y, 0.025);
    float boss = fluorSlab((0.9 - mix(0.05, 0.15, gFluorForm) - abs(f)) * across, y, 0.045);
    float d = wire;
    float paint = 0.381966 * sheet;
    if (boss < d) {
        d = boss;
        paint += 0.5;
    }
    return vec2(d, paint);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    gFluorStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.0);
}
