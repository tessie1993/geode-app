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

// Hollow Ribs: a flight down the inside of a ribcage. An oval hull, a rib
// every unit of the way standing out from it, and a spine along the top that
// the ribs spring from.
//
// Morph leans the ribs over from hoops into one long spiral, and fattens
// them. Worst case 90 steps, no noise.

vec2 hollowMap(vec3 p) {
    vec2 o = p.xy * vec2(0.8, 1.0);
    float r = length(o);
    float th = atan(o.y, o.x);
    float hull = 1.7 - r;
    float lean = p.z + 2.0 * gHollowForm * th / HOLLOW_TAU;
    float z = lean - floor(lean + 0.5);
    float rib = length(vec2(r - 1.55, z * 1.2)) - (0.12 + 0.12 * gHollowForm);
    float spine = length(vec2(o.x, o.y - 1.5)) - 0.22 - 0.06 * cos(HOLLOW_TAU * p.z);
    float d = hollowSmin(hull, hollowSmin(rib, spine, 0.2), 0.25);
    return vec2(d, 5.0 * th / HOLLOW_TAU * 4.0 + 0.5);
}

//#include lib_hollow_march

void main() {
    vec2 uv = view();
    hollowSetup();
    gHollowStride = 0.75;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = hollowFinish(hollowTrace(hollowEye(ro), rd), 1.0);
}
