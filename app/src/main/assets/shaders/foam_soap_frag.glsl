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

// Foam Soap: the packing as a froth of soap bubbles. Every shell is a thin
// clear film whose colour shifts with the angle it is seen at, brightest at
// the rim; the gathered glow behind shows through.
//
// ---- audio ------------------------------------------------------------------
//
// A slower fall (half of lib_foam's), thinner films, and harmony weighs twice
// as much in the bubble size: a held chord blows the froth into big round
// bubbles, a noisy passage breaks it into fine foam. Hats glint on the films
// in their own colour.

void main() {
    vec2 uv = view();
    foamSetup();
    gFoamFall = 0.5;
    gFoamShell *= 0.5;
    gFoamInvS += 0.16 * (foamSat(uHarmony) - 0.5);
    FoamHit h;
    vec3 col = foamBoreFrame(uv, 2, h);
    col = foamAtmosphere(col, h, 0.16, 1.4);
    fragColor = foamFinish(col);
}
