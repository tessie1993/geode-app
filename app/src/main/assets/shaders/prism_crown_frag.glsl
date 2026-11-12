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

// Prism Crown: a lotus crown of chrome blades about a mirror ball, turned
// about. Three rings of thin leaf-shaped blades rise round the ball and lean
// out as they rise: eight tall ones, eight between them, lower and wider,
// and sixteen short ones lying out at the foot; the rings turn slowly against
// each other. A line of light runs up every blade's midrib, where it crosses
// its mirror, and round its edge.
//
// Morph opens the crown: the blades lean further out, from a closed bud to a
// wide dish. Worst case 108 steps, one fold per ring, no noise.

float crownRing(vec3 p, float n, float rad, float len, float cup, float wid, out float seam) {
    vec2 q = prismFold(p.xz, n);
    float y = clamp(p.y, 0.0, len);
    float lean = rad + cup * y * y;
    // The lean and the cup bend the blade, so its distance is scaled back to a bound.
    float blade = (abs(q.x - lean) - 0.03) / sqrt(1.0 + 4.0 * cup * cup * y * y);
    float width = wid * sin(PRISM_PI * y / len);
    float outline = max(q.y - width, max(-p.y, p.y - len));
    seam = min(q.y, abs(q.y - width));
    return max(blade, outline);
}

vec2 prismMap(vec3 p) {
    p.y += 0.9;
    float open = mix(0.15, 0.6, gPrismForm);
    float spin = uTime * PRISM_RATE(35);
    vec3 a = p;
    a.xz = prismRot(spin) * a.xz;
    vec3 b = p;
    b.xz = prismRot(PRISM_PI / 8.0 - spin) * b.xz;
    vec3 c = p;
    c.xz = prismRot(2.0 * spin) * c.xz;
    float s0;
    float s1;
    float s2;
    float d0 = crownRing(a, 8.0, 0.75, 2.1, open, 0.3, s0);
    float d1 = crownRing(b + vec3(0.0, 0.25, 0.0), 8.0, 1.0, 1.5, 1.5 * open, 0.4, s1);
    float d2 = crownRing(c + vec3(0.0, 0.45, 0.0), 16.0, 1.25, 0.9, 3.0 * open, 0.2, s2);
    float ball = length(p - vec3(0.0, 0.55, 0.0)) - 0.5;
    float d = min(min(d0, d1), min(d2, ball));
    float seam = d0 < d1 ? s0 : s1;
    seam = d2 < min(d0, d1) ? s2 : seam;
    seam = ball < min(min(d0, d1), d2) ? abs(p.y - 0.55) : seam;
    return vec2(d, seam);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    gPrismStride = 0.6;
    vec3 ro;
    vec3 rd;
    camAround(uv, 6.5, CAM_SPIN_UNIT, ro, rd);
    fragColor = prismFinish(prismTrace(ro, rd), 1.2);
}
