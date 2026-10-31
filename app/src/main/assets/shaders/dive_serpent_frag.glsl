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

// Dive Serpent: the beam lattice with thinner girders, flown on the serpent
// path - the eye snakes side to side (and a little up and down) through the
// channel, always looking along its own heading, so the girders sweep past
// in long curves. Coloured by depth: hue runs down the tunnel in bands.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (sndShaped(SND_LOW), slewed) WIDENS THE SWING: a heavy passage throws the
// serpent wide across the channel, a quiet one straightens it. Kick (SND_KICK_ENV)
// thickens the girders and pushes the knuckles' hue round the wheel. Palette
// switching as in lib_dive. Brightness follows SND_INTENSITY mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock. Complexity: uMorph adds X braces.
// Chaos: uWarp/uTurbulence bend the girders. Energy: uIntensity scales the
// swing and the kick. Colour: diveColor by depth. Space: the rig's look rides
// on top of the serpent's heading.

void main() {
    vec2 uv = view();
    diveSetup(DIVE_LATTICE);
    float e = diveEnergy();
    gDiveBeam = 0.09 + 0.05 * gDiveKick;
    gDiveSway = 0.7 + 0.5 * clamp(sndShaped(SND_LOW) * e, 0.0, 1.2);

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_SERPENT, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 col = diveBackdrop(rd);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        float mat = gDiveHitMat;
        float hue = 0.045 * hitT + 0.3 * mat + (mat > 0.5 ? 0.3 * gDiveKick : 0.0);
        col = diveShade(n, rd, hitT, used, diveColor(hue), 32.0);
    }

    col *= 0.85 + 0.2 * clamp(SND_INTENSITY, 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
