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

// Opal Domes: a flight low over a plain of glass domes, one to a cell, each
// ringed with fine level lines like the growth rings of a shell.
//
// Morph raises the domes from shallow blisters to tall spires. Worst case 80
// steps and 4 skins, no noise.

vec2 opalMap(vec3 p) {
    vec2 c = floor((p.xz + 1.0) * 0.5);
    vec2 q = p.xz + 1.0 - 2.0 * c - 1.0;
    float h = opalHash(c - vec2(0.0, 8.0) * floor(c.y / 8.0));
    float rise = mix(0.45, 2.4, gOpalForm) * (0.6 + 0.4 * h);
    float y = p.y + 0.8;
    float rad = 0.6 + 0.3 * h;
    float dome = (length(vec3(q, y / rise)) - rad) * min(1.0, rise);
    float d = opalSmin(y, dome, 0.3);
    return vec2(d, 7.0 * y / rise + 0.5);
}

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
