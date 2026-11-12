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
//#include lib_bulb

// Bulb Slice: a power-10 bulb cut open like a specimen. A plane facing the
// eye removes the near half, and the eye sits just in front of the cut, so
// the frame is the CUT FACE edge to edge: the inside of the set, which no
// surface render ever shows, painted as a scan - a posterised heat map of
// the orbit trap, flat bands of colour like a geological section or a
// false-colour MRI. Where the plane passes outside the set, the eye looks
// past it into the dark cavity of the far half.
//
// Grid cell INSIDE x FLAT TRAP (see lib_bulb). Nothing is lit: the face is
// pure trap colour in hard steps, and the far half's skin is a flat dark tone
// darkened only by the free step-count occlusion. No normal at all. It is the
// family's only frame with no depth cue on its main subject: a flat map, where
// every other bulb is a body or a room.
//
// The plane is an intersection (max with the bulb's estimate), which is
// 1-Lipschitz, so the march needs no extra care. A hit belongs to the cut
// face when the plane term is the one that won the max. The eye is always on
// the removed side of the plane, so it can never start inside the solid.
//
// ---- audio ------------------------------------------------------------------
//
// The KICK drives the knife. Each kick (SND_KICK_ENV) pushes the slice plane deeper
// into the body and it drifts back out as the kick decays, so every beat
// reveals a new section of the interior, and the heat map re-forms under it;
// the eye follows the knife in. The plane's tilt wanders on uOrbit,
// re-targeted on section boundaries. Brightness follows
// (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the knife's breathing. Complexity (uMorph) adds up to two fractal
// iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps and twists
// space before the fractal (bulbChaos, estimate divided by bulbChaosLip).
// Energy (uIntensity) scales the kick's push on the knife. Colour is tone(),
// the survey ramp pal() tints. Space is the rig: camAround's turn and look
// orient the knife, and the eye sits BS_GAP in front of the cut along it.

#define BS_MAX_STEPS 128
#define BS_BOUND 1.22
#define BS_CAM_R 3.1
/** How far in front of the cut the eye sits: close enough that the face fills the frame. */
#define BS_GAP 0.85
/** Heat bands across the trap's range. */
#define BS_BANDS 7.0
const float BS_SPIN = 3.0 * CAM_SPIN_UNIT;

vec3 gN;
float gH;
int gIters;

// Its own ramp: a geological survey's, from basalt blue through ochre to
// a hot vermilion.
vec3 tone(float t) {
    return bulbRamp(t, vec3(0.55, 0.45, 0.40), vec3(0.45, 0.35, 0.35), vec3(0.8, 0.8, 0.8), vec3(0.55, 0.65, 0.80));
}

/** The style's body: the bulb cut by the plane, its orbit trap, and in mat the bulb's own distance. */
BulbHit bulbMap(vec3 p) {
    vec4 tr;
    float db = bulbDE(bulbChaos(p), 10.0, 10.0, vec2(0.0), gIters, tr) / bulbChaosLip();
    return BulbHit(max(db, dot(p, gN) - gH), tr, db);
}

//#include lib_bulb_field

void main() {
    vec2 uv = view();
    float swell = clamp((0.5 * (SND_BASS_PRESENCE * uSoundDrive + SND_MID_PRESENCE * uSoundDrive)), 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV * uSoundDrive, 0.0, 1.0);
    gIters = bulbIters(6);

    // The rig's turn picks the knife's direction; the knife faces it, tilted
    // by uOrbit, and the eye sits just in front of the cut looking straight in.
    vec3 ro0;
    vec3 rd0;
    camAround(vec2(0.0), BS_CAM_R, BS_SPIN, ro0, rd0);
    gN = normalize(normalize(ro0) + vec3(0.25 * uOrbit.x, 0.18 * uOrbit.y, 0.0));
    gH = 0.22 + 0.06 * sin(bulbTime() * 0.13) - 0.45 * kick * bulbEnergy();
    vec3 eye = gN * (gH + BS_GAP * (1.0 - 0.2 * uCamDolly));
    vec3 ro;
    vec3 rd;
    bulbAim(uv, eye, eye - gN, ro, rd);
    // Grow and layers (lib_grow): the body at the origin is the subject, measured from this eye, kept off it.
    growSetup(ro, 3.0, true);

    float hue = 0.05 * uOrbit.y;

    float hitT = -1.0;
    float used = 0.0;
    float cutW = 0.0;
    vec4 trap = vec4(1.0);
    // The grown body (lib_grow: a twin aside, children out of the crust, a layer opened) reaches past the
    // bound by growSlack while a control is on; BS_BOUND itself at rest.
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BS_BOUND + growSlack(BS_BOUND, 1.0, 0.2));
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BS_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            vec3 p = ro + rd * t;
            BulbHit h = bulbField(p);
            float d = h.d;
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = h.trap;
                cutW = step(h.mat, dot(p, gN) - gH);
                break;
            }
            t += max(d * 0.9, eps * 0.5);
        }
    }

    // The empty side: a flat near-black of the ramp's cold end.
    vec3 col = tone(hue + 0.05) * 0.04;

    if (hitT > 0.0) {
        float heat = clamp(1.6 * sqrt(trap.x) + 0.8 * trap.y, 0.0, 3.0);
        if (cutW > 0.5) {
            // The scan: the trap as heat, in hard flat bands; Escape colour (lib_shade) adds the smoothed
            // escape count to the heat, so the bands follow the escape too and stay flat.
            float band = floor((heat + 3.0 * shadeEscape(trap.w)) * BS_BANDS / 3.0) / BS_BANDS;
            col = tone(hue + 0.9 * band) * 0.95;
        } else {
            // The far half's skin, seen past the plane: flat and dark.
            col = tone(hue + 0.3 * heat + shadeEscape(trap.w)) * 0.16 * bulbStepAO(used);
        }
    }
    // The step glow (lib_shade): a halo where a ray crept along the skin, by Bloom.
    col += tone(hue + 0.5) * 0.4 * shadeStepGlow(used, uSteps);

    col *= 0.82 + 0.2 * swell;
    fragColor = vec4(grade(col), 1.0);
}
