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

// Foam Ink: the fall drawn in ink on paper. Each shell is an outline that
// thickens toward its silhouette, the cut rings are drawn as hard lines, the
// far bore washes back into the paper, and the only colour is the ring train,
// laid on like watercolour. The light end of the family; no tone map, so the
// paper stays paper.
//
// ---- audio ------------------------------------------------------------------
//
// lib_foam's defaults; each nesting level's own band presses its ink harder,
// and the gathered glow is a grey wash that deepens with the drop.

void main() {
    vec2 uv = view();
    foamSetup();
    vec3 ro;
    vec3 rd;
    foamCamera(uv, ro, rd);
    FoamHit h = foamMarch(ro, rd, FOAM_FAR);
    vec3 paper = vec3(0.93, 0.9, 0.84);
    vec3 col = paper;
    if (h.t > 0.0) {
        FoamSurf s = foamSurface(ro, rd, h);
        float w = 2.0 * s.px;
        float line = 1.0 - smoothstep(0.0, w, abs(s.cut));
        float edge = smoothstep(0.35, 0.95, s.fres + 0.15 * s.lit);
        float shade = 0.12 * (1.0 - s.head) + 0.2 * (1.0 - s.ao);
        float ink = foamSat(max(edge, line) * (0.75 + 0.25 * s.lit) + shade);
        col = paper * (1.0 - 0.88 * ink);
        col = mix(col, paper, 1.0 - exp(-h.t * 0.25));
    }
    col *= 1.0 - 0.3 * (1.0 - exp(-h.glow * 4.0)) * (0.5 + 0.5 * gFoamSurgeBump);
    float wash = (1.0 - exp(-h.rings * 5.0)) * (0.2 + 0.7 * gFoamOnset);
    col = mix(col, col * gFoamAccent, foamSat(wash));
    vec2 nd = vUv * 2.0 - 1.0;
    col *= 1.0 - 0.08 * dot(nd, nd);
    fragColor = vec4(grade(col), 1.0);
}
