#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_dive

// Dive Lattice: a straight dive down the open channel of an endless beam
// lattice - square girders along all three axes, an octahedral knuckle at
// every crossing - lit by a headlamp that rides with the eye and fading into
// a haze that brightens toward the far end. The family's free style and its
// cheapest: one scene, one march, one normal, the straight path.
//
// ---- audio ------------------------------------------------------------------
//
// Kick (uHits.x) swells every girder a little, so the whole frame tightens
// round the channel on the beat and relaxes. Each knuckle is coloured from
// its cell's hash; the palette switches on section changes and every 16
// onsets, crossfading (lib_dive). Brightness follows uSwell mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock. Complexity: uMorph braces the side faces
// with X struts. Chaos: uWarp/uTurbulence bend the girders (diveChaos).
// Energy: uIntensity scales the kick swell. Colour: diveColor (the cosine
// palettes with pal()). Space: the rig - Fly is the dive speed, the look
// turns the view.

void main() {
    vec2 uv = view();
    diveSetup(DIVE_LATTICE);

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_STRAIGHT, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 col = diveBackdrop(rd);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        vec3 base = diveMaterial(gDiveHitMat, gDiveHitId, hitT);
        col = diveShade(n, rd, hitT, used, base, 24.0);
    }

    col *= 0.85 + 0.2 * clamp(uSwell, 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
