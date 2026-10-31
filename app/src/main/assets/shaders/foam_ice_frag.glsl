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

// Foam Ice: a slow fall through frozen foam. Thick frosted beads, pale and
// cold, bright at the rim, under a cold white end light; frost glitters on
// every shell.
//
// ---- audio ------------------------------------------------------------------
//
// Half lib_foam's fall, thicker shells, and the frost is the sound's high end:
// the hats AND the snare (whichever is louder) raise the glitter, so a
// backbeat cracks across the ice. Treble sharpens the rim's highlight.

void main() {
    vec2 uv = view();
    foamSetup();
    gFoamFall = 0.5;
    gFoamShell = 0.02 * (1.0 + 0.6 * gFoamKick);
    gFoamHats = max(gFoamHats, gFoamSnare);
    vec3 ro;
    vec3 rd;
    foamCamera(uv, ro, rd);
    FoamHit h = foamMarch(ro, rd, FOAM_FAR);
    vec3 cold = vec3(0.72, 0.86, 1.0);
    vec3 col = h.tEnd > FOAM_FAR ? cold * (0.05 + 0.6 * pow(foamSat(dot(rd, gFoamFwd)), 30.0)) : cold * 0.05;
    if (h.t > 0.0) {
        FoamSurf s = foamSurface(ro, rd, h);
        vec3 frost = mix(cold, foamColor(0.55 + 0.2 * s.trapR), 0.25);
        float spec = pow(foamSat(dot(reflect(rd, s.n), gFoamFwd)), 30.0 + 90.0 * gFoamTreble);
        col = frost * (0.12 + 0.55 * s.head) * (0.7 + 0.4 * s.lit) * s.ao;
        col += vec3(0.9, 0.96, 1.0) * s.fres * 0.7 * s.ao + vec3(spec) * 0.6;
        col += foamAccents(s, vec3(1.0), 2.0);
    }
    col = foamAtmosphere(col, h, 0.25, 0.6);
    fragColor = foamFinish(col);
}
