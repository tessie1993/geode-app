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

// Hollow Roots: a flight through a burrow that roots have grown across. Five
// roots wander the length of it, winding round the way and round each other,
// swelling and thinning as they go.
//
// Morph grows the roots: from threads to trunks that crowd the burrow. Worst
// case 72 steps, no noise.

vec2 hollowMap(vec3 p) {
    float r = length(p.xy);
    float th = atan(p.y, p.x);
    float d = 2.3 + 0.25 * sin(3.0 * th + 0.7853982 * p.z) - r;
    for (int i = 0; i < 5; i++) {
        float f = float(i);
        float zc = 0.3926991 * p.z * float(1 + i / 2) + 1.7 * f;
        vec2 c = (0.85 + 0.2 * f) * vec2(cos(zc + 1.26 * f), sin(zc * (1.0 + mod(f, 2.0)) + 2.5 * f));
        float thick = (0.08 + 0.26 * gHollowForm) * (1.0 + 0.4 * sin(1.5707963 * p.z + 2.0 * f));
        // Roots keep clear of the way.
        c *= max(1.0, (0.45 + thick) / max(length(c), 1.0e-3));
        d = hollowSmin(d, length(p.xy - c) * 0.75 - thick, 0.35);
    }
    return vec2(d, 2.5 * p.z + 1.5 * sin(2.0 * th) + 0.5);
}

//#include lib_hollow_march

void main() {
    vec2 uv = view();
    hollowSetup();
    gHollowStride = 0.5;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = hollowFinish(hollowTrace(hollowEye(ro), rd), 1.0);
}
