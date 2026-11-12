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
//#include lib_prism

// Prism Fan: a flight through a row of chrome fans, one every two units of
// the way. Each is seven thin curved vanes set round an open middle inside a
// chrome hoop, pitched like a turbine's and swept back like a shutter's
// blades; one fan in two is pitched and turned the other way. Lines of light
// run along every vane's edges and round each hoop's rims.
//
// Morph closes the shutter, the vanes reaching in toward the way. Worst case
// 108 steps, one fold, no noise.

#define FAN_N 7.0

vec2 prismMap(vec3 p) {
    float cell = floor(p.z * 0.5 + 0.5);
    float zl = p.z - 2.0 * cell;
    float side = mod(cell, 2.0) * 2.0 - 1.0;
    float r = length(p.xy);
    float pitch = 1.4;
    float sweep = 0.5;
    vec2 v = prismRot(side * (pitch * zl + uTime * PRISM_RATE(45)) + sweep * r) * p.xy;
    vec2 q = prismFold(v, FAN_N);
    // The vanes' twist and sweep stretch the fold, so their distance is scaled back to a bound.
    float vane = (q.y - 0.03) / sqrt(1.0 + r * r * (pitch * pitch + sweep * sweep));
    float open = mix(1.25, 0.55, gPrismForm);
    float d = max(vane, max(abs(zl) - 0.35, max(open - r, r - 2.5)));
    float hoop = length(vec2(r - 2.56, zl)) - 0.07;
    float seam = d < hoop ? min(abs(r - open), abs(abs(zl) - 0.35)) : abs(zl);
    d = min(d, hoop);
    // Each fan keeps within 0.45 of its middle, and the next one turns another way.
    d = min(d, 1.55 - abs(zl));
    return vec2(d, seam);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    gPrismStride = 0.75;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.2);
}
