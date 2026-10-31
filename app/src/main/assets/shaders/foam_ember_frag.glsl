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

// Foam Ember: a bore through cooling slag. The big shells are a dark crust
// with a red rim; the small deep beads are still molten and glow from inside,
// so the cavities behind the wall are lit like a furnace. Sparks drift on the
// hats.
//
// ---- audio ------------------------------------------------------------------
//
// lib_foam's defaults, plus: the bass (sndShaped(SND_LOW), slewed) stokes the
// molten beads, each nesting level's own band adds to its heat, the kick's
// bead swell is doubled (the beads are what glows), and the sparks are the
// hats' sparkles at a higher gain in the fire's colour.

vec3 emberColor(float x) {
    return mix(foamColor(x), vec3(1.0, 0.42, 0.1), 0.6);
}

void main() {
    vec2 uv = view();
    foamSetup();
    gFoamShell = 0.012 * (1.0 + 1.6 * gFoamKick);
    vec3 ro;
    vec3 rd;
    foamCamera(uv, ro, rd);
    FoamHit h = foamMarch(ro, rd, FOAM_FAR);
    vec3 col = h.tEnd > FOAM_FAR ? emberColor(0.05) * 0.3 * pow(foamSat(dot(rd, gFoamFwd)), 12.0) : vec3(0.02, 0.005, 0.0);
    if (h.t > 0.0) {
        FoamSurf s = foamSurface(ro, rd, h);
        // Molten in the deep beads, and in veins along each level's band of the trap.
        float molten = max(smoothstep(3.0, 6.5, s.scaleLog), 0.6 * smoothstep(0.75, 0.95, 1.0 - abs(fract(s.trapR * 4.0) - 0.5) * 2.0));
        float heat = molten * (0.45 + 1.2 * gFoamBass + 0.6 * s.lit);
        vec3 crust = vec3(0.035, 0.025, 0.02) * (0.3 + 0.7 * s.head) + emberColor(0.0) * s.fres * 0.35;
        col = crust * s.ao + emberColor(0.02 + 0.1 * s.trapR) * heat;
        col += foamAccents(s, vec3(1.0, 0.6, 0.2), 1.6);
    }
    col = foamAtmosphere(col, h, 0.22, 0.35);
    fragColor = foamFinish(col);
}
