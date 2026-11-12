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
//#include lib_flame

// Flame Web: a flight between funnels of spider silk. Each cell of a square
// lattice holds a vortex: nested trumpet surfaces about its axis, and twelve
// spokes that lean into a spiral; only where spoke meets trumpet is lit, so
// each vortex is a web strung in the dark. Neighbours wind opposite ways.
//
// Morph winds the spokes from straight to a tight whirl. Worst case 72
// samples, no folds, no noise.

void flameField(vec3 p, out vec2 f, out vec2 hue, out vec2 wt) {
    vec2 cell = floor(p.xy * 0.25);
    vec2 c = p.xy - 4.0 * cell - 2.0;
    float side = mod(cell.x + cell.y, 2.0) * 2.0 - 1.0;
    float r = max(length(c), 1.0e-3);
    float th = atan(c.y, c.x) * side;
    float lr = log(r);
    f = vec2(0.5 * p.z * side + 1.5 * lr, 12.0 * th / FLAME_TAU + mix(0.2, 3.0, gFlameForm) * lr + uTime * FLAME_RATE(50));
    hue = vec2(0.25 * lr + 0.3 * side, 0.5 + 0.2 * lr);
    float edge = smoothstep(2.0, 1.5, max(abs(c.x), abs(c.y))) * smoothstep(0.03, 0.2, r);
    wt = edge * vec2(0.9, 0.7);
}

void main() {
    vec2 uv = view();
    flameSetup();
    gFlameThread = 0.9;
    gFlamePeriod.y = 12.0;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, FLAME_PERIOD);
    fragColor = flameFinish(flameMarch(ro, rd), 0.6);
}
