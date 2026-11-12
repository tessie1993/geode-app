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

// Opal Melt: a flight between a ceiling and a floor of slow liquid. Drops hang
// from above and rise from below, one to a cell, each on its own slow clock,
// their necks thinning as they fall.
//
// Morph stretches the drops until they meet in the middle and stand as
// columns. Worst case 80 steps and 4 skins, no noise.

vec2 opalMap(vec3 p) {
    float side = p.y < 0.0 ? -1.0 : 1.0;
    float y = abs(p.y);
    vec2 c = floor((p.xz + 1.0) * 0.5);
    vec2 q = p.xz + 1.0 - 2.0 * c - 1.0;
    float h = opalHash(c - vec2(0.0, 8.0) * floor(c.y / 8.0));
    float fall = 0.5 + 0.5 * sin(uTime * OPAL_RATE(500) + 6.28 * h + 1.9 * side);
    float reach = mix(0.35 + 0.75 * h * fall, 1.75, gOpalForm);
    float slab = 1.75 - y;
    float drop = length(vec3(q, (y - 1.75 + reach) * 1.3)) - (0.28 + 0.14 * h);
    float neck = max(length(q) - (0.07 + 0.2 * gOpalForm + 0.06 * h), 1.75 - reach - y);
    float d = opalSmin(slab, opalSmin(drop, neck, 0.35), 0.45);
    return vec2(d, 0.5);
}

//#include lib_opal_march

void main() {
    vec2 uv = view();
    opalSetup();
    gOpalStride = 0.7;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, OPAL_PERIOD);
    fragColor = opalFinish(opalTrace(ro, rd), 1.0);
}
