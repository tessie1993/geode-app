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

// Foam Neon: the bore's wall is dark, and only the circles where it cuts each
// sphere glow, so the tunnel is lined with neon rings of every size, nested
// inside each other. A wider, gentler bend, so the rings stream past rather
// than swing.
//
// ---- audio ------------------------------------------------------------------
//
// lib_foam's defaults, with the onset rings brighter (they are the only other
// light) and the gathered glow halved so the cut rings stay crisp. Each ring
// is lit by its nesting level's own band, so a bassline lights the big rings
// and the hats the small ones.

void main() {
    vec2 uv = view();
    foamSetup();
    gFoamRadius *= 1.1;
    gFoamBend = 0.5;
    gFoamOnset = min(gFoamOnset * 1.4, 1.0);
    FoamHit h;
    vec3 col = foamBoreFrame(uv, 1, h) * 0.9;
    col = foamAtmosphere(col, h, 0.12, 0.5);
    fragColor = foamFinish(col);
}
