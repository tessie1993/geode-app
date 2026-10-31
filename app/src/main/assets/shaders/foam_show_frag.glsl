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

// Foam Show: the family's three looks in turn. Each section of the music is
// one of pearl and chrome, neon cut rings or soap film, and a section change
// cross-fades the material on the same surface while the kaleidoscope steps,
// so the bore changes what it is made of without a cut.
//
// ---- audio ------------------------------------------------------------------
//
// lib_foam's defaults. The look is SND_CUT_COUNT mod 3, faded from the last
// one across SND_HELD_HARD_CUT; the surface is shaded twice only while that
// fade runs.

vec3 showLook(FoamSurf s, float look) {
    if (look < 0.5) return foamPearl(s);
    if (look < 1.5) return foamNeon(s);
    return foamSoap(s);
}

void main() {
    vec2 uv = view();
    foamSetup();
    float lookNew = mod(gFoamSection, 3.0);
    float lookOld = mod(gFoamSection - 1.0, 3.0);
    vec3 ro;
    vec3 rd;
    foamCamera(uv, ro, rd);
    FoamHit h = foamMarch(ro, rd, FOAM_FAR);
    vec3 col = h.tEnd > FOAM_FAR ? foamBackdrop(rd) : foamColor(0.62) * 0.1;
    if (h.t > 0.0) {
        FoamSurf s = foamSurface(ro, rd, h);
        col = showLook(s, lookNew);
        if (gFoamWedgeHold > 0.001) col = mix(col, showLook(s, lookOld), gFoamWedgeHold);
    }
    col = foamAtmosphere(col, h, 0.18, 1.0);
    fragColor = foamFinish(col);
}
