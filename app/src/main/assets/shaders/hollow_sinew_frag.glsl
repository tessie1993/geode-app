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

// Hollow Sinew: the family's opener. A flight down a passage of living tissue:
// the wall is cords of sinew wound along it, and tendons cross the passage
// from wall to wall beside the way, webbed into the wall where they land.
//
// Morph tightens the weave: the cords deepen and wind steeper, the tendons
// thicken. Worst case 72 steps, no noise.

vec2 hollowMap(vec3 p) {
    float r = length(p.xy);
    float th = atan(p.y, p.x);
    float cord = sin(6.0 * th + 0.7853982 * p.z + 1.5 * sin(0.3926991 * p.z));
    float wall = 1.55 + 0.3 * sin(0.7853982 * p.z + th) - r - (0.1 + 0.2 * gHollowForm) * cord;
    // One tendon to every two units of the way, each turned from the last, passing beside the axis.
    float k = floor(p.z * 0.5);
    vec3 q = vec3(p.xy, p.z - 2.0 * k - 1.0);
    q.xy = hollowRot(2.4 * mod(k, 8.0)) * q.xy;
    float tendon = length(vec2(q.x - 0.75, q.z + 0.3 * sin(1.5 * q.y))) - (0.07 + 0.13 * gHollowForm) * (1.0 + 0.5 * q.y * q.y);
    float d = hollowSmin(wall, tendon, 0.6);
    return vec2(d, 3.0 * th / HOLLOW_TAU * 6.0 + 0.75 * p.z + 0.6 * cord);
}

void main() {
    vec2 uv = view();
    hollowSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = hollowFinish(hollowTrace(hollowEye(ro), rd), 1.0);
}
