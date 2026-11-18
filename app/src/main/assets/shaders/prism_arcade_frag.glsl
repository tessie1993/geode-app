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

// Prism Arcade: a flight down a corridor of arches in a hall of mirrors. Two
// chrome walls, an arch cut through them every two units, stand on a chrome
// floor; across the way the corridor repeats, each one the mirror image of the
// next, so every arch opens on another arcade and another beyond it. A line of
// light runs round every arch, along the walls' tops and down the middle of
// each corridor's floor.
//
// Morph draws the arches up from round to pointed.
//
// Formula, three halls (lib_strata): 0 the arcade, as it always was; 0.5 its
// walls cut by a Menger sponge, square windows within square windows between
// the arches, gStrataIters folds; 1 tracery, an arch within every arch and
// one within that, each a little over half the last, the last growing in.
// The far fractal follows: a kaleidoscope dust, a Menger cube, an Apollonian
// ball, seen past the halls and in the chrome.
//
// Worst case 108 steps, the sponge's at most 3 folds or 3 nested arches a
// step, no noise; then the layers.

/** The hall at p (x the distance, y the ribbon's): `nest` arches within each arch, a Menger cut through the walls when `pierce`. */
vec2 hall(vec3 p, float nest, bool pierce) {
    float xr = abs(p.x - 2.6 * floor(p.x / 2.6 + 0.5));
    float wall = 1.22 - xr;
    float zz = p.z - 2.0 * floor(p.z * 0.5 + 0.5);
    float a = 0.62;
    float o = 0.6 * a * gPrismForm;
    vec2 u = vec2(abs(zz), p.y - 0.1);
    float open = u.y > 0.0 ? length(u + vec2(o, 0.0)) - (a + o) : u.x - a;
    float d = max(max(wall, -open), p.y - 1.9);
    // The arches' line keeps to the walls; the floor carries only its middle line.
    float seam = min(min(abs(open) + max(1.0 - xr, 0.0), abs(p.y - 1.9)), xr);
    if (pierce) {
        // The wall lies in the sponge's outer layer, where its cut is a carpet; one sponge to an arch.
        float holes = strataHoles(vec3(xr - 0.63, p.y - 0.4, zz), 1.0, gStrataIters);
        seam = min(seam, abs(holes) + max(1.0 - xr, 0.0));
        d = max(d, -holes);
    }
    float s = 1.0;
    for (int i = 0; i < 3; i++) {
        float f = clamp(nest - float(i), 0.0, 1.0);
        if (f <= 0.0) break;
        // The next arch in, on the same sill: its frame a thin wall standing in the opening.
        s *= 0.58;
        vec2 v = u / s;
        float inner = (v.y > 0.0 ? length(v + vec2(o, 0.0)) - (a + o) : v.x - a) * s;
        float frame = max(max(wall, abs(inner) - 0.035 * f), max(open, p.y - 1.9));
        d = min(d, frame);
        seam = min(seam, abs(inner) + max(1.0 - xr, 0.0));
    }
    return vec2(min(d, p.y + 1.1), seam);
}

vec2 prismMap(vec3 p) {
    vec2 a = vec2(1.0e3);
    vec2 b = a;
    vec2 c = a;
    if (strataUses(0)) a = hall(p, 0.0, false);
    if (strataUses(1)) b = hall(p, 0.0, true);
    if (strataUses(2)) c = hall(p, gStrataIters, false);
    return strataBlend2(a, b, c);
}

//#include lib_prism_march

void main() {
    vec2 uv = view();
    prismSetup();
    strataSetup(vec3(0.0, 3.0, 2.0), 0.0, 0.45, 0.7);
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = prismFinish(prismTrace(prismEye(ro), rd), 1.0);
}
