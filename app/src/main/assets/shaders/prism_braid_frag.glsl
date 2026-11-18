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
//#include lib_strata
//#include lib_prism

// Prism Braid: a flight down a braid of chrome bands. Five thin bands wind
// round the way one way and seven wind round them the other, and both rings
// swell and draw in along the way like a stream over stones. Each band lies
// across a mirror, so a band tipped over comes back as a creased chevron;
// lines of light run down both edges of every band and along its crease.
//
// Morph tips the bands over, from flat ribbons facing the axis to deep
// chevrons.
//
// Formula, three braids (lib_strata): 0 the flat bands, as they always were;
// 0.5 every band pierced along its length by a Menger sponge, a ladder of
// square windows within square windows, gStrataIters folds; 1 molten: the
// bands drawn round as tubes and the two rings run together by a smooth
// union, swelling wherever they cross. The far fractal follows: a Mandelbox,
// a Menger cube, a kaleidoscope dust.
//
// Worst case 108 steps, one fold per ring and at most 3 sponge folds a ring,
// no noise; then the layers.

/** One ring of n bands about the way at radius rad, tipped `tilt`: x the distance, y the ribbon's. `kind` 0 bands, 1 pierced bands, 2 tubes. */
vec2 braidBands(vec2 v, float z, float n, float rad, float tilt, float kind) {
    vec2 q = prismFold(v, n);
    vec2 l = prismRot(tilt) * vec2(q.x - rad, q.y);
    float seam = min(abs(abs(l.y) - 0.32), q.y);
    if (kind > 1.5) return vec2(length(vec2(l.x, max(abs(l.y) - 0.24, 0.0))) - 0.08, min(abs(l.y), q.y));
    float d = prismBox(l, vec2(0.025, 0.32));
    if (kind > 0.5) {
        // One sponge to every 0.8 along the way (dividing the period); the band in its outer layer.
        float holes = strataHoles(vec3(l.y, z - 0.8 * floor(z / 0.8 + 0.5), l.x + 0.27), 0.4, gStrataIters);
        seam = min(seam, abs(holes));
        d = max(d, -holes);
    }
    return vec2(d, seam);
}

vec2 braid(vec3 p, float kind) {
    float zc = p.z * (PRISM_TAU / PRISM_PERIOD);
    float tilt = 1.1 * gPrismForm;
    float swell = 0.18 * sin(2.0 * zc + uTime * PRISM_RATE(110));
    vec2 a = braidBands(prismRot(2.0 * zc) * p.xy, p.z, 5.0, 1.35 + swell, tilt, kind);
    vec2 b = braidBands(prismRot(0.3 - zc) * p.xy, p.z, 7.0, 2.1 - swell, -tilt, kind);
    if (kind > 1.5) {
        // The smooth union: where the rings cross they run together.
        float k = 0.35;
        float h = max(k - abs(a.x - b.x), 0.0) / k;
        return vec2(min(a.x, b.x) - h * h * k * 0.25, a.x < b.x ? a.y : b.y);
    }
    return a.x < b.x ? a : b;
}

vec2 prismMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = braid(p, 0.0);
    if (strataUses(1)) b = braid(p, 1.0);
    if (strataUses(2)) c = braid(p, 2.0);
    return strataBlend2(a, b, c);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    strataSetup(vec3(1.0, 3.0, 0.0), 1.0, 0.45, 0.7);
    gPrismStride = 0.55;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.1);
}
