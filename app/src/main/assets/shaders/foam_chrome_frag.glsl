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

// Foam Chrome: every sphere a mirror. The shells reflect the light at the end
// of the bore and the ring train, tinted by their nesting level, with a dark
// line at each silhouette so the packing stays legible. A tighter twist than
// Foam Fall, so the reflections spiral.
//
// ---- audio ------------------------------------------------------------------
//
// lib_foam's defaults, plus: the reflected end light swells with the drop's
// bump, the tint band of each level brightens with that level's spectrum band,
// and the onset rings appear IN the mirrors (the reflected ray reads the ring
// train at the hit), so a hit flashes down every surface at once.

void main() {
    vec2 uv = view();
    foamSetup();
    gFoamTwist = 3.0;
    vec3 ro;
    vec3 rd;
    foamCamera(uv, ro, rd);
    FoamHit h = foamMarch(ro, rd, FOAM_FAR);
    vec3 col = h.tEnd > FOAM_FAR ? foamBackdrop(rd) : foamColor(0.62) * 0.1;
    if (h.t > 0.0) {
        FoamSurf s = foamSurface(ro, rd, h);
        vec3 r = reflect(rd, s.n);
        vec3 tint = mix(vec3(1.0), foamColor(0.1 + 0.45 * s.trapR + 0.2 * s.trapX), 0.45);
        vec3 env = foamBackdrop(r) * (1.8 + 1.2 * gFoamSurgeBump);
        // The ring train seen in the mirror, a short way along the reflected ray.
        env += gFoamAccent * foamRingAt(s.p + r * 0.6) * (0.2 + 0.9 * gFoamOnset);
        col = env * tint * (0.5 + 0.7 * s.lit);
        col *= mix(0.15, 1.0, smoothstep(0.0, 0.25, s.head)) * s.ao;
        float spec = pow(foamSat(dot(r, gFoamFwd)), 40.0 + 80.0 * gFoamTreble);
        col += vec3(spec) * 0.8 * s.ao;
        col += foamAccents(s, vec3(1.0), 0.7);
    }
    col = foamAtmosphere(col, h, 0.18, 0.7);
    fragColor = foamFinish(col);
}
