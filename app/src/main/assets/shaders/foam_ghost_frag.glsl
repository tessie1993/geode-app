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

// Foam Ghost: the packing in X-ray. The ray never stops at a shell: it walks
// through them, gathering light from every one it crosses, so the nested
// spheres behind the wall show through as glowing outlines, level inside
// level. No surface, no normal; the cost is a fixed walk of up to 96 steps.
//
// ---- audio ------------------------------------------------------------------
//
// lib_foam's defaults; each shell crossed glows with its nesting level's own
// band, so the spectrum reads in depth: the bass lights the deep spheres, the
// hats the near shells. The ring train passes through the whole volume.

#define GH_STEPS 96
#define GH_FAR 9.0

void main() {
    vec2 uv = view();
    foamSetup();
    vec3 ro;
    vec3 rd;
    foamCamera(uv, ro, rd);
    float t = 0.05 + hash13(vec3(gl_FragCoord.xy, 5.0)) * 0.05;
    vec3 acc = vec3(0.0);
    float rings = 0.0;
    for (int i = 0; i < GH_STEPS; i++) {
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = foamMap(p);
        float ad = abs(gFoamD);
        // Inside the bore step by the bore's distance; among the shells, by the
        // distance to the nearest one either side, floored so the walk goes on.
        float stepLen = gFoamMask < 0.5 ? max(d, 0.03) : max(ad * 0.8, 0.025);
        float trap = foamSat(sqrt(min(gFoamOrb.w, FOAM_TRAP_FAR)) * 0.75);
        float shell = exp(-ad * 45.0) * gFoamMask;
        acc += foamColor(0.2 + 0.6 * trap) * shell * stepLen * (0.5 + 0.9 * sndSpectrum(trap)) * exp(-0.12 * t);
        rings += foamRingAt(p) * shell * stepLen;
        t += stepLen;
        if (t > GH_FAR) break;
    }
    vec3 col = foamBackdrop(rd) * 0.4 + acc * 6.0;
    col += gFoamAccent * (1.0 - exp(-rings * 8.0)) * (0.2 + 0.8 * gFoamOnset);
    fragColor = foamFinish(col);
}
