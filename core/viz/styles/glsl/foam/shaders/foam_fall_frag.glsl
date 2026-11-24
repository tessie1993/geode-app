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
//#include lib_sdf3
//#include lib_foam

// Foam Fall: the family's opener. A fall down a curving bore through the
// sphere packing, banking into its bends, the big shells chrome-bright with the
// end light and the small deep beads solid pearl.
//
// ---- audio ------------------------------------------------------------------
//
// Every default of lib_foam, unchanged: loudness sets the fall, harmony and
// brightness the bubble size, flux the writhe, a build closes the bore and the
// drop bursts it, kicks swell the beads, onsets run rings down the bore, each
// nesting level is lit by its own band, treble tightens the gloss and a
// section change steps the kaleidoscope. Hats sparkle on the shells.

void main() {
    vec2 uv = view();
    foamSetup();
    FoamHit h;
    vec3 col = foamBoreFrame(uv, 0, h);
    col = foamAtmosphere(col, h, 0.2, 1.0);
    fragColor = foamFinish(col);
}
