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

// Opal Ribbon: a flight between two glass ribbons wound round the way ahead,
// each turning on its own length as it goes, ruled with fine lines along it.
//
// Morph widens the ribbons until they close into one twisted pipe. Worst case
// 100 steps and 4 skins, no noise.

vec2 opalMap(vec3 p) {
    float off = dot(p.xy, p.xy);
    p.xy = opalRot(0.3926991 * p.z) * p.xy;
    p.xy *= p.x < 0.0 ? -1.0 : 1.0;
    float a = atan(p.y, p.x);
    vec2 q = vec2(p.x - 1.3, p.y);
    q = opalRot(0.3926991 * p.z) * q;
    float wide = mix(0.65, 0.0, gOpalForm);
    float band = length(vec2(max(abs(q.x) - wide, 0.0), q.y)) - 0.05;
    float pipe = abs(length(p.xy) - 1.3) - 0.05;
    float d = mix(band, pipe, smoothstep(0.5, 1.0, gOpalForm));
    // The winding stretches space away from the axis: the distance is scaled back to a bound.
    d /= sqrt(1.0 + 0.6 * off);
    return vec2(d, mix(6.0 * q.x, 12.0 * a / OPAL_TAU, smoothstep(0.5, 1.0, gOpalForm)) + 0.5);
}

//#include lib_opal_march

void main() {
    vec2 uv = view();
    opalSetup();
    gOpalStride = 0.8;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, OPAL_PERIOD);
    fragColor = opalFinish(opalTrace(ro, rd), 1.3);
}
