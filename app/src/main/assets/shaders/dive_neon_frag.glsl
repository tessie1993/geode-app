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

// Dive Neon: the beam lattice as pure light. No surface is lit: the march
// itself gathers glow from every girder it passes close to, so thin girders
// read as neon tubes with a soft bloom round each and the far lattice melts
// into a coloured haze. Very cheap - no normal, no shading, a coarse stride.
//
// ---- audio ------------------------------------------------------------------
//
// An onset (SND_ONSET_ENV) pushes a hue wave into the near girders that fades back
// with depth and envelope - the colour moves, the brightness does not. Kick
// (SND_KICK_ENV) thickens the tubes a little. Palette switching as in lib_dive.
// Brightness follows SND_INTENSITY mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock. Complexity: uMorph adds X braces (more
// tubes). Chaos: uWarp/uTurbulence bend them. Energy: uIntensity scales the
// hue wave and the kick. Colour: diveColor. Space: the rig.

#define NE_MAX_STEPS 96
#define NE_FAR 28.0

void main() {
    vec2 uv = view();
    diveSetup(DIVE_LATTICE);
    float e = diveEnergy();
    gDiveBeam = 0.035 + 0.02 * gDiveKick;
    float wave = clamp(SND_ONSET_ENV * e, 0.0, 1.0);

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_STRAIGHT, uv, ro, rd);

    vec3 glow = vec3(0.0);
    float tm = 0.05;
    for (int i = 0; i < NE_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (tm > NE_FAR) break;
        float d = diveMap(ro + rd * tm);
        vec3 c = diveColor(0.35 * gDiveId + 0.25 * gDiveMat + 0.03 * tm + 0.4 * wave * exp(-0.15 * tm));
        float fade = exp(-0.07 * tm);
        glow += c * exp(-max(d, 0.0) * 14.0) * 0.06 * fade;
        if (d < 0.004) {
            glow += c * 0.6 * fade;
            break;
        }
        tm += max(d * 0.9, 0.03);
    }

    vec3 col = diveBackdrop(rd) * 0.5 + glow;
    col *= 0.85 + 0.2 * clamp(SND_INTENSITY, 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
