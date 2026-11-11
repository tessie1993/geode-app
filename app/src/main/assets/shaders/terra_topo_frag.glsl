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
//#include lib_terra

// Terrain Topo: the family's mountains as a survey relief model, flown over.
// The same ridged field as Terrain Ridge, now marched in 3D and seen from high
// and steep, the way you lean over a raised-relief map: the ground is painted
// with the chart's hypsometric tints (green lowland, tan uplands, brown
// heights, pale summits), the contour lines are draped over the real slopes
// with every fifth one an index contour drawn heavier, the valleys below the
// lowest level fill with flat lakes ringed by a blue shoreline, and a faint
// graticule is ruled across land and water alike. The sun stands high in the
// north-west, the cartographer's light, so the hill-shade reads as it did on
// the flat map. With distance the survey fades into blank paper: the fog and
// the sky are the sheet's own colour.
//
// The contours are lines of a value, not of the plane: fwidth() of the level
// sets their width in pixels, so they stay one weight near and far and over
// any slope. Where one pixel would span more than about half a level the lines
// fade out instead of crawling into moire. Every fwidth() here is called at
// the top level of main(), in uniform control flow; only the values it reads
// are set inside the hit branch.
//
// ---- camera -----------------------------------------------------------------
//
// terraFly over camFly: the eye TP_EYE units up, tipped TP_TILT down so the
// land fills most of the frame and the horizon sits near the top. The rig's
// travel is the flight north across the chart. The field tiles in z with
// TERRA_PERIOD (512, which divides the rig's 8192 wrap), so the wrap never
// jumps the land; the graticule is ruled on unwrapped world xz every 8 units,
// and 8 divides 8192, so it does not jump either.
//
// ---- the distance estimate --------------------------------------------------
//
// It is a heightfield march (lib_terra's pattern): the step is the vertical
// clearance above ground(xz) = max(land, lake level), scaled by k = 0.45 for
// the ridged field's steepest slopes and divided by the Chaos warp's stretch
// (terraStep). The max() with a flat water plane can only lower the slope, so
// the same k is safe for it. terraSlab skips every ray above the highest peak,
// and that peak is a true bound: 1.5 * relief * (1 + bass lift), all three
// clamped. The eye flies above it for every input (min eye 4.4 - 0.42 against
// a peak of at most 2.23). A step that lands under the surface is pulled back
// by terraRefine. The loop is bounded by TP_MAX_STEPS and the Detail budget.
//
// ---- audio ------------------------------------------------------------------
//
// Beats (SND_KICK_TOGGLE) THICKEN THE ISOLINES: every contour swells on the beat
// and thins back as the envelope falls, so the whole relief pulses its ink in
// time - line weight, not brightness. Bass (SND_BASS_PRESENCE) raises the land
// a little, so contours crowd up the slopes and the lakes shrink on low notes.
// The key (uKeyHue, gated by uKeyStrength) drifts the tints. Brightness
// follows SND_ENERGY_TREND only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the Chaos drift and the tint drift. Complexity
// (uMorph) fades in an octave of ground (terraOctaves). Chaos (uWarp,
// uTurbulence, uTwist) bends the ridges (terraChaos). Energy (uIntensity)
// scales the beat's ink and the bass lift. Colour is pal() in the tints, the
// lines and the water. Space is the rig.
//
// ---- knobs (uKnob, each read as its offset from 0.5) --------------------------
//
// k0 PULSE how much the beat thickens the lines. k1 TEXTURE the contour
// interval (7 to 13 levels over the range). k2 FLOW the lake level. k3 TONE
// shifts the tints' hue. k4 BUILD exaggerates the relief (0.85 to 1.15).
// k5 SPACE thickens the paper haze and the graticule.

#define TP_MAX_STEPS 128
#define TP_FAR 38.0
#define TP_EYE 4.4
#define TP_TILT 0.42
#define TP_LEVELS 10.0
#define TP_LAKE 0.16

float gOct;
float gK;
float gEx;
float gLake;

/** The land: the ridged field, scaled by the relief and the bass lift. */
float land(vec2 xz) {
    return 1.5 * gEx * gK * terraRidged(terraChaos(terraWrap(xz)), gOct);
}

/** What the ray meets: the land, or the lake's flat water where the land dips under it. */
float ground(vec2 xz) {
    return max(land(xz), gLake);
}

void main() {
    vec2 uv = view();
    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float swell = clamp(SND_ENERGY_TREND * uSoundDrive, 0.0, 1.5);
    float energy = terraEnergy();
    float beat = clamp(SND_KICK_TOGGLE * energy, 0.0, 1.0);
    float lift = 0.12 * clamp(SND_BASS_PRESENCE * uSoundDrive, 0.0, 1.2) * energy;
    gOct = terraOctaves(3.0);
    gK = 1.0 + lift;
    gEx = 1.0 + 0.3 * kBuild;
    float range = 1.5 * gEx;
    // The lake is set against the unlifted range, so the bass lift drains it.
    gLake = (TP_LAKE + 0.06 * kFlow) * range;
    float hMax = range * gK;
    float hue = 0.03 * sin(terraTime() * 0.02) + 0.1 * uKeyStrength * sin(TERRA_TAU * uKeyHue) + 0.16 * kTone;

    vec3 ro;
    vec3 rd;
    terraFly(uv, TP_EYE, TP_TILT, ro, rd);

    // The cartographer's light, high in the north-west; the sky is the sheet.
    vec3 sun = normalize(vec3(-0.6, 0.7, 0.4));
    vec3 sunCol = mix(vec3(1.0, 0.96, 0.88), pal(hue + 0.1), 0.12);
    vec3 paper = mix(vec3(0.88, 0.86, 0.80), pal(hue + 0.3), 0.08);
    vec3 zenith = mix(vec3(0.52, 0.64, 0.80), pal(hue + 0.6) * 0.8, 0.15);

    float hitT = -1.0;
    float used = 0.0;
    vec2 span = terraSlab(ro, rd, hMax, TP_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < TP_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > span.y) break;
            used = float(i);
            vec3 p = ro + rd * t;
            float dy = p.y - ground(p.xz);
            if (dy < terraEps(t)) {
                hitT = terraRefine(tPrev, dyPrev, t, dy);
                break;
            }
            tPrev = t;
            dyPrev = dy;
            t += terraStep(dy, t, 0.45);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    float tt = hitT > 0.0 ? hitT : TP_FAR;
    vec3 p = ro + rd * tt;
    float th = gLake;
    vec3 n = vec3(0.0, 1.0, 0.0);
    if (hitT > 0.0) {
        th = land(p.xz);
        if (th > gLake) n = TERRA_NORMAL(land, p.xz, 0.003 * hitT + 0.01);
    }

    // Derivatives first, in uniform control flow: the contour level, the
    // shoreline and the graticule all measure themselves in pixels.
    float hn = th / range;
    float levels = TP_LEVELS + 6.0 * kTexture;
    float v = hn * levels;
    float fw = max(fwidth(v), 1e-4);
    float fwShore = max(fwidth(th), 1e-4);
    vec2 fwGrid = max(fwidth(p.xz), vec2(1e-4));

    vec3 col = terraSky(rd, sun, zenith, paper, sunCol * 0.5);
    if (hitT > 0.0) {
        float wet = th > gLake ? 0.0 : 1.0;

        // Hypsometric tints, each pulled a little toward the palette.
        vec3 tint = mix(vec3(0.42, 0.58, 0.38), vec3(0.80, 0.72, 0.52), smoothstep(0.25, 0.55, hn));
        tint = mix(tint, vec3(0.62, 0.46, 0.34), smoothstep(0.55, 0.78, hn));
        tint = mix(tint, vec3(0.94, 0.93, 0.90), smoothstep(0.8, 0.95, hn));
        tint = mix(tint, pal(hue + 0.6 * hn), 0.22);
        float dif = max(dot(n, sun), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        vec3 ground3 = tint * (0.3 + 0.9 * dif) * sunCol + tint * sky * zenith * 0.3;
        ground3 *= terraStepAO(used);

        // The lakes: flat water, the sheet's sky in it at grazing angles, a glint.
        vec3 water = mix(vec3(0.42, 0.62, 0.78), pal(hue + 0.6) * 0.7, 0.2);
        vec3 rr = reflect(rd, vec3(0.0, 1.0, 0.0));
        float fres = 0.08 + 0.6 * pow(1.0 - max(-rd.y, 0.0), 5.0);
        water = mix(water * 0.9, terraSky(rr, sun, zenith, paper, sunCol * 0.5), fres);
        water += sunCol * 0.6 * pow(max(dot(rr, sun), 0.0), 80.0);
        col = mix(ground3, water, wet);

        // Contours: distance to the nearest level, in pixels of that level,
        // faded where the levels crowd under one pixel.
        float dl = abs(fract(v - 0.5) - 0.5) / fw;
        float di = abs(fract(v / 5.0 - 0.5) - 0.5) * 5.0 / fw;
        float wid = 0.6 + (1.4 + 1.6 * kPulse) * beat;
        float line = 1.0 - smoothstep(wid, wid + 1.0, dl);
        float index = 1.0 - smoothstep(wid * 1.9, wid * 1.9 + 1.0, di);
        float crowd = 1.0 - smoothstep(0.3, 0.7, fw);
        vec3 ink = mix(vec3(0.36, 0.22, 0.12), pal(hue + 0.05) * 0.3, 0.25);
        col = mix(col, ink, max(line * 0.55, index * 0.85) * crowd * (1.0 - wet));
        float shore = 1.0 - smoothstep(0.8, 1.8, abs(th - gLake) / fwShore);
        col = mix(col, vec3(0.16, 0.36, 0.58), shore * 0.8 * (1.0 - smoothstep(0.3, 0.8, fwShore / range * levels)));

        // The graticule: a grid every 8 world units, faded where it would alias.
        vec2 g = abs(fract(p.xz / 8.0 - 0.5) - 0.5) * 8.0 / fwGrid;
        float gFade = 1.0 - smoothstep(0.6, 1.6, max(fwGrid.x, fwGrid.y));
        col = mix(col, vec3(0.2, 0.3, 0.45), (1.0 - smoothstep(0.4, 1.2, min(g.x, g.y))) * (0.3 + 0.3 * kSpace) * gFade);

        // The survey fades into blank paper with distance.
        col = terraFog(col, rd, hitT, 0.045 * (1.0 + 0.8 * kSpace), paper, sun, sunCol * 0.4);
    }

    col *= 0.85 + 0.17 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.4, 2.4, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
