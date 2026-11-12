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

// Prism Nest: three chrome spheres one inside the next, turned about. Each
// thin shell is pierced by fourteen round windows, a ring of six above its
// waist, six below and one at each pole, and each shell turns on its own
// axis, so the windows of the inner shells drift past those of the outer ones
// and every shell mirrors its neighbours. Lines of light ring every window and
// each shell's waist.
//
// Morph opens the windows until each shell is more lattice than wall. Worst
// case 108 steps, one fold per shell, no noise.

float nestShell(vec3 p, float rad, float hole, out float seam) {
    vec2 q = prismFold(p.xz, 6.0);
    vec3 f = vec3(q.x, abs(p.y), q.y);
    // One window per mirror, 35 degrees above or below the waist, and one at each pole.
    float side = length(f - rad * vec3(0.819, 0.574, 0.0)) - hole;
    float pole = length(f - vec3(0.0, rad, 0.0)) - 1.2 * hole;
    float win = min(side, pole);
    seam = min(abs(win), abs(p.y));
    return max(abs(length(p) - rad) - 0.035, -win);
}

vec2 prismMap(vec3 p) {
    float open = mix(0.18, 0.36, gPrismForm);
    vec3 a = p;
    a.xz = prismRot(uTime * PRISM_RATE(50)) * a.xz;
    vec3 b = p;
    b.yz = prismRot(0.6 - uTime * PRISM_RATE(65)) * b.yz;
    vec3 c = p;
    c.xy = prismRot(1.1 + uTime * PRISM_RATE(100)) * c.xy;
    float s0;
    float s1;
    float s2;
    float d0 = nestShell(a, 2.6, open * 2.6, s0);
    float d1 = nestShell(b, 1.75, open * 1.75, s1);
    float d2 = nestShell(c, 1.0, open * 1.0, s2);
    float d = min(d0, min(d1, d2));
    float seam = d0 < d1 ? s0 : s1;
    seam = d2 < min(d0, d1) ? s2 : seam;
    return vec2(d, seam);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    vec3 ro;
    vec3 rd;
    camAround(uv, 7.0, CAM_SPIN_UNIT, ro, rd);
    fragColor = prismFinish(prismTrace(ro, rd), 1.1);
}
