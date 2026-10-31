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

// Foam Kaleido: a straight bore, so the view down it is a mandala: twice the
// wedges of the rest of the family, a tighter twist and a faster turn, the cut
// rings glowing over a dim pearl wall.
//
// ---- audio ------------------------------------------------------------------
//
// lib_foam's defaults, with the section change doing more: each new section
// doubles a wedge count of 3 to 8 (6 to 16 wedges), morphed across the held
// cut. The mid clock turns the mandala three times as fast as Foam Fall.
// The user's Kaleidoscope control, when on, sets the count instead.

void main() {
    vec2 uv = view();
    foamSetup();
    gFoamBend = 0.0;
    gFoamTwist = 4.0;
    gFoamSpin *= 3.0;
    if (uKaleido < 0.5) {
        gFoamWedgesNew *= 2.0;
        gFoamWedgesOld *= 2.0;
    }
    vec3 ro;
    vec3 rd;
    foamCamera(uv, ro, rd);
    FoamHit h = foamMarch(ro, rd, FOAM_FAR);
    vec3 col = h.tEnd > FOAM_FAR ? foamBackdrop(rd) : foamColor(0.62) * 0.1;
    if (h.t > 0.0) {
        FoamSurf s = foamSurface(ro, rd, h);
        col = foamNeon(s) + 0.35 * foamPearl(s);
    }
    col = foamAtmosphere(col, h, 0.15, 0.8);
    fragColor = foamFinish(col);
}
