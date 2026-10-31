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
//#include lib_dive

// Dive Mandala: a straight dive down a kaleidoscopic tunnel. The cross-
// section is folded into a whole number of mirrored wedges, and every wedge
// carries the same ornament - a petal on the wall, a bead on each wedge edge
// at every ridge ring, a thin ray reaching in toward the axis - so the
// tunnel reads as a mandala from straight on and as carved relief in depth.
// Whole fold counts only: atan's jump lands on a wedge border, so no seam.
//
// ---- audio ------------------------------------------------------------------
//
// The beat envelope (SND_KICK_TOGGLE) swells every petal at once and lets it
// settle, so the mandala breathes on the beat. Petals and beads take hue by
// their distance from the axis, so the rings of colour stay concentric.
// Palette switching as in lib_dive. Brightness follows SND_ENERGY_TREND mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock. Complexity: uMorph steps the fold count up
// from 6 to 12 wedges, crossfading at each step. Chaos: uWarp/uTurbulence
// bend the tunnel. Energy: uIntensity scales the petal swell. Colour:
// diveColor. Space: the rig.

void main() {
    vec2 uv = view();
    diveSetup(DIVE_MANDALA);

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_STRAIGHT, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 col = diveBackdrop(rd);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        float mat = gDiveHitMat;
        float rad = length(p.xy);
        vec3 base = diveColor(0.2 * mat + 0.45 * rad);
        if (mat < 0.5) base *= 0.35;
        col = diveShade(n, rd, hitT, used, base, mat > 1.5 ? 48.0 : 20.0);
        // Gilded accents: beads and rays catch the lamp hard.
        if (mat > 1.5) col += diveColor(0.9) * 0.25 * max(dot(n, -rd), 0.0) * exp(-0.055 * hitT);
    }

    col *= 0.85 + 0.2 * clamp(SND_ENERGY_TREND, 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
