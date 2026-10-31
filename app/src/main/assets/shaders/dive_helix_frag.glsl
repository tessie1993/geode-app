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

// Dive Helix: the jewel chambers flown on a helix - the eye corkscrews round
// the tunnel's axis, looking along its heading, the horizon rocking a little
// with it (a bounded roll that always comes back, never a spin). The stones
// here are dark and glassy: almost all of their colour is reflection and an
// oily sheen that turns with the angle of view.
//
// ---- audio ------------------------------------------------------------------
//
// A drop (SND_HELD_SURGE) WIDENS THE HELIX: the corkscrew opens out on the drop
// and tightens back over its 2.5 s envelope. Hats (SND_HAT_ENV) glint a hashed
// third of the facets, as in Dive Jewel. Palette switching as in lib_dive.
// Brightness follows SND_LEVEL mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock. Complexity: uMorph cuts a second ring of
// star facets. Chaos: uWarp/uTurbulence bend the cut. Energy: uIntensity
// scales the drop and the glints. Colour: diveColor. Space: the rig's look
// rides on top of the helix.

void main() {
    vec2 uv = view();
    diveSetup(DIVE_JEWEL);
    float e = diveEnergy();
    gDiveHelixR = 0.45 + 0.35 * clamp(SND_HELD_SURGE * e, 0.0, 1.0);

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_HELIX, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 col = diveBackdrop(rd);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        float id = gDiveHitId;
        float ndv = max(dot(n, -rd), 0.0);
        vec3 base = diveMaterial(gDiveHitMat, id, hitT) * 0.15;
        col = diveShade(n, rd, hitT, used, base, 80.0);
        float fog = exp(-0.055 * hitT);
        // Sheen: hue runs with the view angle, strongest at grazing.
        vec3 sheen = diveColor(id * 0.5 + 1.6 * ndv);
        col += sheen * (0.08 + 0.5 * pow(1.0 - ndv, 2.0)) * fog;
        vec3 refl = reflect(rd, n);
        float pick = step(0.66, dive_hash11(id * 91.0 + fract(SND_ONSET_COUNT * 0.618) * 37.0));
        float glint = pick * clamp(SND_HAT_ENV * e, 0.0, 1.0) * pow(max(dot(refl, -rd), 0.0), 6.0);
        col += vec3(glint * 0.6 * fog);
    }

    col *= 0.85 + 0.2 * clamp(SND_LEVEL, 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
