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
//#include lib_opal

// Opal Bubbles: the family's opener. A flight through a cloud of soap bubbles,
// one to a cell of a lattice, each its own size and a little off its centre;
// the way ahead runs between them.
//
// Morph swells the bubbles until neighbours touch and their walls join into
// one foam. Worst case 80 steps and 4 skins, no noise.

vec2 opalMap(vec3 p) {
    vec3 c = floor((p + 1.0) * 0.5);
    vec3 q = p + 1.0 - 2.0 * c - 1.0;
    float h = opalHash(c.xy + 17.0 * mod(c.z, 8.0));
    float h2 = fract(h * 57.31);
    q -= 0.22 * vec3(h - 0.5, h2 - 0.5, fract(h * 13.7) - 0.5) * (1.0 - gOpalForm);
    q.y += 0.06 * sin(uTime * OPAL_RATE(900) + 6.28 * h);
    float ball = length(q) - mix(0.38 + 0.36 * h2, 0.98, gOpalForm);
    // The lattice's own wall, where swollen neighbours meet.
    vec3 w = cos(p * 1.5707963);
    float wall = 0.9 - 0.75 * gOpalForm + 0.45 * (w.x * w.x + w.y * w.y + w.z * w.z) - 0.9;
    float d = mix(ball, max(ball, -wall), smoothstep(0.55, 1.0, gOpalForm));
    return vec2(d, 0.5);
}

//#include lib_opal_march

void main() {
    vec2 uv = view();
    opalSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, OPAL_PERIOD);
    fragColor = opalFinish(opalTrace(ro, rd), 1.0);
}
