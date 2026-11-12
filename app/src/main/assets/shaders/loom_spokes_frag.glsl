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
//#include lib_loom

// Loom Spokes: a flight through flat wheel webs strung across the way at
// intervals, the way passing through the open hub of each: sixteen spokes
// from a hub ring to an eight-sided frame, beaded with light, a spiral
// thread laid across them, each web turned from the last.
//
// Morph spins the webs: the spiral winds out from the hub until it reaches
// the frame. Worst case 90 steps, no noise.

vec4 loomMap(vec3 p) {
    float k = floor(p.z * 0.25 + 0.5);
    float z = p.z - 4.0 * k;
    float turn = mod(k, 4.0);
    vec2 q = loomRot(1.3 * turn) * p.xy;
    float r = length(q);
    float th = loomAngle(q);
    float rad = 0.022;
    float hue = 0.17 * turn;
    // The spokes, out to the frame.
    float ds = loomRep(th, LOOM_TAU / 16.0);
    float reach = 3.0 / cos(loomRep(th - ds, LOOM_TAU / 8.0));
    float rs = r - clamp(r, 0.8, reach);
    vec3 s = vec3(length(vec3(r * sin(ds), rs, z)) - rad, 3.0 * r, hue);
    // The spiral, spaced `lap` apart, wound out from the hub as far as Morph lets it.
    float lap = 0.16;
    float sp = loomRep((r - 0.9) / lap - th / LOOM_TAU, 1.0) * lap;
    float rr = r - clamp(r, 0.95, mix(1.3, 2.9, gLoomForm));
    s = loomNearer(s, vec3(length(vec3(sp, rr, z)) - 0.8 * rad, 8.0 * th / LOOM_TAU, hue + 0.35));
    // The hub ring and the frame.
    float hub = length(vec2(r - 0.8, z)) - 1.6 * rad;
    float frame = length(vec2(r * cos(loomRep(th, LOOM_TAU / 8.0)) - 3.0, z)) - 1.6 * rad;
    s = loomNearer(s, vec3(min(hub, frame), 2.0 * loomAngle(vec2(r - 0.8, z)) / LOOM_TAU + 4.0 * th / LOOM_TAU, hue + 0.7));
    return vec4(s.x, s.y, 1.0e3, s.z);
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 1.2);
}
