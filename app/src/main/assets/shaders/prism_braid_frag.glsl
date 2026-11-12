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

// Prism Braid: a flight down a braid of chrome bands. Five thin bands wind
// round the way one way and seven wind round them the other, and both rings
// swell and draw in along the way like a stream over stones. Each band lies
// across a mirror, so a band tipped over comes back as a creased chevron;
// lines of light run down both edges of every band and along its crease.
//
// Morph tips the bands over, from flat ribbons facing the axis to deep
// chevrons. Worst case 108 steps, one fold per ring, no noise.

float braidBands(vec2 v, float n, float rad, float tilt, out float seam) {
    vec2 q = prismFold(v, n);
    vec2 l = prismRot(tilt) * vec2(q.x - rad, q.y);
    seam = min(abs(abs(l.y) - 0.32), q.y);
    return prismBox(l, vec2(0.025, 0.32));
}

vec2 prismMap(vec3 p) {
    float zc = p.z * (PRISM_TAU / PRISM_PERIOD);
    float tilt = 1.1 * gPrismForm;
    float swell = 0.18 * sin(2.0 * zc + uTime * PRISM_RATE(110));
    float s0;
    float s1;
    float d0 = braidBands(prismRot(2.0 * zc) * p.xy, 5.0, 1.35 + swell, tilt, s0);
    float d1 = braidBands(prismRot(0.3 - zc) * p.xy, 7.0, 2.1 - swell, -tilt, s1);
    return vec2(min(d0, d1), d0 < d1 ? s0 : s1);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    gPrismStride = 0.55;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.1);
}
