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
//#include lib_bulb

// Bulb Lotus: the LATITUDE variant of the bulb. Where the classic formula
// multiplies the polar angle (measured from the pole), this one multiplies the
// latitude (measured from the equator) - the other reading of "double the
// angle" - and the body comes out as a flower rather than a seed head: five
// petals round the pole (azimuth power 5) folding up and over each other.
// The latitude power is a real number and moves freely, because latitude has
// no wrap; only the azimuth power has to stay an integer (see lib_bulb).
//
// Grid cell MACRO x GLOW (see lib_bulb). The eye hovers just over the bloom
// and looks down into its heart, so the frame is petals, never the flower's
// outline. Nothing is lit by a normal: the flower glows from within. Light
// pours from the pole axis (the trap's closeness to it), the thin petal edges
// (the trap's closeness to the equatorial plane) and the folds (where the
// ray crawled, by its step count), and every march step that passed near a
// petal adds a little haze of its own, so the gaps between petals are veils
// of light rather than holes. The family's one HIGH-KEY, low-contrast frame:
// pastel on pale haze, soft everywhere. No normal also means four fractal
// evaluations fewer per pixel.
//
// ---- audio ------------------------------------------------------------------
//
// PRESENCE opens the flower. The presence band (sndShaped(SND_HIGH) - voice and lead
// instruments) raises the latitude power from 2.2 toward 4.0, so the petals
// unfold and multiply while someone sings and close back into a bud in the
// gaps; the eye sinks a little toward the heart as they open. Hats
// (SND_HAT_ENV) shiver the azimuth phase, a tremble through the petals.
// Brightness follows SND_ENERGY_TREND only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the petal twirl. Complexity (uMorph) adds up to two fractal
// iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps and twists
// space before the fractal (bulbChaos, estimate divided by bulbChaosLip).
// Energy (uIntensity) scales the presence bloom and the hat tremble. Colour is
// tone(), the blossom ramp pal() tints. Space is the rig: its travel circles
// the eye round the pole, its look and shake ride on top (bulbAim), the dolly
// lowers it toward the heart.

#define BL_MAX_STEPS 112
#define BL_BOUND 1.45
#define BL_M 5.0
/** The eye's height over the bloom at rest; presence and the dolly lower it. */
#define BL_EYE_H 1.68
/** How far off the pole the eye circles, and where on the bloom it looks. */
#define BL_EYE_OFF 0.42
#define BL_AIM_OFF 0.22
#define BL_AIM_Y 0.30
#define BL_GLOW_K 10.0
const float BL_SPIN = -3.0 * CAM_SPIN_UNIT;

float gLat;
float gTwirl;
int gIters;

// Its own ramp: blush, peach and a pale jade, all kept light.
vec3 tone(float t) {
    return bulbRamp(t, vec3(0.80, 0.62, 0.66), vec3(0.20, 0.22, 0.18), vec3(1.0), vec3(0.00, 0.15, 0.55));
}

float lotusDE(vec3 p, out vec4 tr) {
    vec3 z = p;
    float dr = 1.0;
    float r = max(length(z), 1e-6);
    float grow = max(gLat, BL_M);
    float esc = float(gIters);
    tr = vec4(dot(p, p), abs(p.y), length(p.xz), 1.0);
    for (int i = 0; i < BULB_MAX_ITERS; i++) {
        if (i >= gIters) break;
        r = max(length(z), 1e-6);
        if (r > 2.0) {
            esc = float(i);
            break;
        }
        float lat = asin(clamp(z.y / r, -1.0, 1.0)) * gLat;
        float lon = atan(z.z, z.x + 1e-12) * BL_M + gTwirl;
        dr = grow * pow(r, BL_M - 1.0) * dr + 1.0;
        float cl = cos(lat);
        z = pow(r, BL_M) * vec3(cl * cos(lon), sin(lat), cl * sin(lon)) + p;
        tr.x = min(tr.x, dot(z, z));
        tr.y = min(tr.y, abs(z.y));
        tr.z = min(tr.z, length(z.xz));
    }
    r = max(length(z), 1e-6);
    tr.w = esc / float(max(gIters, 1));
    return 0.5 * log(r) * r / dr;
}

/** The style's body: its distance and orbit trap (mat unused). */
BulbHit bulbMap(vec3 p) {
    vec4 tr;
    float d = lotusDE(bulbChaos(p), tr) / bulbChaosLip();
    return BulbHit(d, tr, 0.0);
}

//#include lib_bulb_field

void main() {
    vec2 uv = view();
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float presence = clamp(0.5 * sndShaped(SND_HIGH) + 0.5 * sndShaped(SND_MIDDLE) / 1.5, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float open = clamp(presence * bulbEnergy(), 0.0, 1.0);
    gLat = 2.2 + 1.8 * open;
    gTwirl = 0.05 * hat * bulbEnergy() * sin(bulbTime() * 23.0) + 0.1 * sin(bulbTime() * 0.05);
    gIters = bulbIters(6);

    // MACRO: hover over the heart and look down into it, a little off-axis,
    // circling the pole with the rig's travel.
    float a = uCamTravel * BL_SPIN;
    float h = BL_EYE_H - 0.18 * open - 0.12 * uCamDolly;
    vec3 eye = vec3(BL_EYE_OFF * cos(a), h, BL_EYE_OFF * sin(a));
    vec3 aim = vec3(BL_AIM_OFF * cos(a + 2.4), BL_AIM_Y, BL_AIM_OFF * sin(a + 2.4));
    vec3 ro;
    vec3 rd;
    bulbAim(uv, eye, aim, ro, rd);
    // Grow and layers (lib_grow): the body at the origin is the subject, measured from this eye, kept off it.
    growSetup(ro, 3.0, true);

    float hue = 0.03 * sin(bulbTime() * 0.013);

    float hitT = -1.0;
    float used = 0.0;
    float veil = 0.0;
    vec4 trap = vec4(1.0);
    // The grown body (lib_grow: a twin aside, children out of the crust, a layer opened) reaches past the
    // bound by growSlack while a control is on; BL_BOUND itself at rest.
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BL_BOUND + growSlack(BL_BOUND, 1.0, 0.2));
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BL_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            BulbHit h = bulbField(ro + rd * t);
            float d = h.d;
            // Every step near a petal lights the air a little: the veils.
            veil += exp(-max(d, 0.0) * BL_GLOW_K) * 0.03;
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = h.trap;
                break;
            }
            t += max(d * 0.8, eps * 0.5);
        }
    }

    // Pale haze, lighter overhead, with the veils of light added.
    vec3 haze = mix(vec3(0.62, 0.58, 0.60), mix(tone(hue + 0.2), vec3(1.0), 0.6), smoothstep(-0.6, 0.5, rd.y));
    vec3 glowCol = mix(tone(hue + 0.1), vec3(1.0, 0.92, 0.84), 0.5);
    vec3 col = haze * 0.82 + glowCol * min(veil, 1.0) * 0.35;

    if (hitT > 0.0) {
        // Emission only: the heart, the thin edges and the folds.
        float heart = exp(-trap.z * 4.0);
        float thin = (1.0 - smoothstep(0.0, 0.4, trap.y));
        float fold = smoothstep(0.25, 0.85, used / max(uSteps, 1.0));
        // Escape colour (lib_shade): the smoothed escape count moves the petal along its ramp.
        vec3 petal = mix(tone(hue + 0.35 * trap.y + 0.25 * sqrt(trap.x) + shadeEscape(trap.w)), vec3(1.0), 0.3);
        col = petal * (0.48 + 0.30 * thin + 0.25 * heart);
        col += glowCol * (0.35 * fold + 0.25 * heart * thin);
        col += glowCol * min(veil, 1.0) * 0.2;
        col = mix(col, haze, 1.0 - exp(-max(hitT - 0.5, 0.0) * 0.7));
    }

    col *= 0.88 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
