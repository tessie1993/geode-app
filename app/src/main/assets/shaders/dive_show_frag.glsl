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

// Dive Show: the whole piece - the user's PARALLAX_MODE 0. Scenes AND camera
// paths cycle together: every thirteen seconds the iris closes, and it opens
// on a new scene flown a new way (straight, serpent, helix, breathing
// accordion, orbit look-around), the two cycles out of step so the pairings
// keep changing. The palette switches on drops, sections and runs of onsets.
// Surfaces get the family's full treatment: headlamp, a reflection of the
// coloured haze, and, in the jewel scene, glints on the hats.
//
// ---- audio ------------------------------------------------------------------
//
// Everything the single styles do, live: kick swells girders and pulses
// caps, mids turn gyroscopes, the beat swells petals, bass works the
// accordion and widens the serpent, a drop widens the helix, hats glint the
// facets. A drop (SND_HELD_SURGE) also leans every hue along the palette while it
// rings. Palette switching as in lib_dive. Brightness follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock and both cycles. Complexity: uMorph adds
// each scene's extra layer. Chaos: uWarp/uTurbulence bend the scenes.
// Energy: uIntensity scales every reaction. Colour: diveColor. Space: the
// rig's look rides on top of every path.

void main() {
    vec2 uv = view();
    float seg = diveSegment();
    int scene = int(mod(seg, 5.0));
    // Two steps a segment, one more every five: (scene, path) runs through
    // all twenty-five pairings before it repeats.
    int cam = 1 + int(mod(2.0 * seg + floor(seg / 5.0), 5.0));
    diveSetup(scene);
    float e = diveEnergy();
    if (cam == DIVE_CAM_SERPENT) gDiveSway = 0.6 + 0.4 * clamp(sndShaped(SND_LOW) * e, 0.0, 1.2);
    if (cam == DIVE_CAM_HELIX) gDiveHelixR = 0.4 + 0.25 * clamp(SND_HELD_SURGE * e, 0.0, 1.0);
    if (cam == DIVE_CAM_ACCORDION) gDiveBreath = clamp(0.78 + 0.5 * sndShaped(SND_LOW) * e, 0.75, 1.4);
    if (cam == DIVE_CAM_ORBIT) gDiveSwing = 0.5 + 0.2 * clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float lean = 0.2 * clamp(SND_HELD_SURGE * e, 0.0, 1.0);

    vec3 ro;
    vec3 rd;
    diveCamera(cam, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 col = diveBackdrop(rd);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        float id = gDiveHitId;
        vec3 base = diveMaterial(gDiveHitMat, id + lean, hitT);
        col = diveShade(n, rd, hitT, used, base, 40.0);
        vec3 refl = reflect(rd, n);
        float fog = exp(-0.055 * hitT);
        float fres = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        col += diveColor(0.3 + 0.5 * refl.y + lean) * 0.25 * fres * fog;
        if (scene == DIVE_JEWEL) {
            float pick = step(0.66, dive_hash11(id * 91.0 + fract(floor(SND_HIT_TIME) * 0.618) * 37.0));
            col += vec3(pick * clamp(SND_HAT_ENV * e, 0.0, 1.0) * pow(max(dot(refl, -rd), 0.0), 6.0) * 0.6 * fog);
        }
    }

    col *= diveIris();
    col *= 0.85 + 0.2 * clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
