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

// Dive Accordion: the mushroom tunnel flown straight, with the tunnel itself
// breathing like a bellows - the spacing of the rings stretches and squeezes
// about the eye, so the forest ahead draws in and pushes away while the
// mushrooms beside the camera stay put. Night version: a deep haze, caps lit
// from inside, glowing spots on their crowns.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (sndShaped(SND_LOW)) works the bellows: loud low end stretches the tunnel
// out, a quiet passage squeezes the rings together. Kick (SND_KICK_ENV) pulses the
// caps. The cap glow is constant; only the geometry moves on a hit. Palette
// switching as in lib_dive. Brightness follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock. Complexity: uMorph grows the second ring
// of small mushrooms. Chaos: uWarp/uTurbulence bend the stems. Energy:
// uIntensity scales the bellows and the cap pulse. Colour: diveColor.
// Space: the rig.

void main() {
    vec2 uv = view();
    diveSetup(DIVE_MUSHROOM);
    float e = diveEnergy();
    gDiveBreath = clamp(0.78 + 0.5 * sndShaped(SND_LOW) * e, 0.75, 1.4);

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_ACCORDION, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 col = diveBackdrop(rd) * 0.6;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        float mat = gDiveHitMat;
        float id = gDiveHitId;
        vec3 capCol = diveColor(0.55 + 0.4 * id);
        vec3 base = mat > 0.5 ? capCol * 0.5 : diveColor(0.3) * 0.12;
        col = diveShade(n, rd, hitT, used, base, 12.0) * 0.8;
        if (mat > 0.5 && mat < 1.5) {
            // Spots on the crown: a cheap 3D dot pattern, lit from within.
            vec3 s = p * 6.0;
            float spot = smoothstep(0.55, 0.8, sin(s.x) * sin(s.y) * sin(s.z) * 0.5 + 0.5);
            float crown = smoothstep(0.0, 0.7, -dot(n.xy, normalize(p.xy + 1e-5)));
            col += capCol * (0.12 + 0.5 * spot * crown) * exp(-0.07 * hitT);
        }
    }

    col *= 0.85 + 0.2 * clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
