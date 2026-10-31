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

// Foam Coral: a slow drift through a reef. Fat shells merge into soft, fleshy
// beads lit through their skin (wrapped light with a warm subsurface tint),
// the bore wider and the packing opened up, sliding sideways like a current.
//
// ---- audio ------------------------------------------------------------------
//
// Half lib_foam's fall, larger spheres, twice the slide; the kick's swell is
// what makes the polyps pulse, and the hats are tiny glints on their tips.

void main() {
    vec2 uv = view();
    foamSetup();
    gFoamFall = 0.5;
    gFoamRadius *= 1.15;
    gFoamInvS += 0.08;
    gFoamShell = 0.032 * (1.0 + 0.7 * gFoamKick);
    gFoamShift.xy += 0.4 * vec2(sin(SND_MID_TIME * FOAM_RATE(41.0)), cos(SND_BASS_TIME * FOAM_RATE(29.0)));
    vec3 ro;
    vec3 rd;
    foamCamera(uv, ro, rd);
    FoamHit h = foamMarch(ro, rd, FOAM_FAR);
    vec3 deep = vec3(0.02, 0.05, 0.08);
    vec3 col = h.tEnd > FOAM_FAR ? deep + foamBackdrop(rd) * 0.5 : deep;
    if (h.t > 0.0) {
        FoamSurf s = foamSurface(ro, rd, h);
        vec3 flesh = mix(foamColor(0.02 + 0.3 * s.trapR), vec3(1.0, 0.5, 0.42), 0.45);
        float wrap = (dot(s.n, -rd) + 0.5) / 1.5;
        float through = foamSat(dot(s.n, gFoamFwd) + 0.3);
        col = flesh * (0.1 + 0.6 * foamSat(wrap)) * (0.7 + 0.5 * s.lit) * s.ao;
        col += vec3(1.0, 0.35, 0.25) * through * 0.25 * s.fres;
        col += foamAccents(s, vec3(1.0, 0.9, 0.7), 0.6);
    }
    col = foamAtmosphere(col, h, 0.28, 0.6);
    fragColor = foamFinish(col);
}
