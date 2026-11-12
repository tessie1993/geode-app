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

// Fluor Scales: a low glide over an open floor laid with round scales in
// rows, each row half a scale over from the last. Every scale tilts up toward
// its lip on the near side and lies over the tail of the row before, so the
// floor is a field of glowing arcs, open dark above.
//
// Morph lifts the scales: their lips rise from the floor. Worst case 90 steps,
// six scales a step, no noise.

vec2 fluorMap(vec3 p) {
    float h = p.y + 0.9;
    float lift = mix(0.12, 0.4, gFluorForm);
    // The tilted face's slope, so its distance is a true one.
    float slope = sqrt(1.0 + lift * lift / 1.44);
    float d = 1.0e3;
    float paint = 0.0;
    float row0 = floor(p.z / 0.4);
    for (int j = -1; j <= 1; j++) {
        float row = row0 + float(j);
        float off = 0.25 * mod(row, 2.0);
        float cz = (row + 0.5) * 0.4;
        float kx = floor((p.x - off) / 0.5 + 0.5);
        float cx = off + 0.5 * kx;
        float nx = p.x < cx ? -0.5 : 0.5;
        for (int i = 0; i < 2; i++) {
            vec2 q = vec2(p.x - cx - float(i) * nx, p.z - cz);
            float face = (h - 0.03 - 0.5 * lift * (0.3 - q.y) / 0.6) / slope;
            float scale = max(length(q) - 0.3, face);
            if (scale < d) {
                d = scale;
                paint = 0.381966 * mod(row, 5.0);
            }
        }
    }
    return vec2(fluorSmin(h, d, 0.02), paint);
}

//#include lib_fluor_march

void main() {
    vec2 uv = view();
    fluorSetup();
    gFluorStride = 0.75;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = fluorFinish(fluorTrace(fluorEye(ro), rd), 1.1);
}
