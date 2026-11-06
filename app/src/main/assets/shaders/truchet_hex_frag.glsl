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
//#include lib_truchet

// Truchet Hex: the honeycomb version, laid out as a floor and flown over.
// Every hexagon is a slightly bevelled tile in one of the tiling's two tones
// (its corner caps against its middle), and its three 120-degree arcs are
// real cords: tubes of round cross-section pressed half into the tile, striped
// along their length, so the paths read as braided cord laid on a tiled floor
// that runs to the fog. A light hangs ahead of the eye and sways from side to
// side, raking along the cords so they catch it on their crowns and throw a
// contact shadow on the tiles beside them.
//
// THE CAMERA. The eye rides the rig's path (uCamPos x and z) at a fixed height
// above the floor and looks along the rig's view (uCamBasis) pitched down, so
// Space - path, look, shake, FOV - all apply. The rig's travel wraps at 8192
// along z. The honeycomb is laid with its unit lattice period ALONG z (the
// plan is (z, x) * scale, and truchetHexCell repeats every 1 in its first
// axis), so z can be rebased by 64 world units, which divides 8192: that is
// 64 or 96 whole cell columns at either scale, the tile ids are wrapped by
// the same count (an even one, so the two offset lattices keep their odd and
// even ids), and the Chaos bend and the colour field only use frequencies
// that are whole turns per 64 world units. The rebase and the travel wrap are
// therefore both exact and invisible.
//
// WHY THE DISTANCE ESTIMATE IS SAFE. Distances are taken in cell units and
// divided by the scale at the end. The cord is length(arc distance, height
// above the cord's axis) - radius: a true distance to the full circles of the
// cell's own three arcs (each circle is a superset of its arc, so the value
// never overshoots them). A neighbour's cord can only be closer than that if
// the ray crosses the cell border first, so every step is also clamped to the
// distance to the hexagon's edge plus half a cord radius; at the edge
// midpoints the cords run straight on into the neighbour, so the most a ray
// can sink into one is that half radius, and the next step's negative
// distance catches it. The tile top is a heightfield with a gentle bevel
// (slope under 0.5) and a hit-lifted tile is a prism whose wall the same
// border clamp reaches; its height is scaled by the slope's Lipschitz bound,
// 1 / sqrt(1 + slope^2), as truchet_relief does. The Chaos bend has slope
// under 1, so dividing by 1 + its slope keeps the plan distance a lower bound. Above the slab the
// floor can occupy the march does not run at all; it starts at the slab's top.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets turn a scattered third of the tiles a sixth of a turn (truchetRoll),
// eased over the onset envelope, and the turning tile rises off the floor and
// settles back as it turns, so the cords re-route under the viewer on the hit.
// The mids (sndShaped(SND_MIDDLE)) thicken the cords; the bass presence lifts
// them prouder of the tiles; the stripes slide along the cords on uFlowTime
// (the flow direction follows the honeycomb's vertex classes, so it is
// continuous from hex to hex). The key leans the hue. Brightness follows
// SND_ENERGY_TREND only; the light moves on its own clock.
//
// ---- macros and knobs ---------------------------------------------------------
//
// Speed: uTime drives the light's sway and the hue drift. Complexity: uMorph
// adds stripes and, past 0.5, makes the tiles two thirds the size. Chaos:
// uWarp/uTurbulence bend the floor's plan before it is tiled, so the cords
// meander but stay joined. Energy: uIntensity scales the mid thickening, the
// bass lift and the roll's rise. Colour: tiles, cords and light are pal().
// Space: the camera rig. Knobs: k0 PULSE the roll's rise, k1 TEXTURE the stripe
// contrast, k2 FLOW the cord thickness, k3 TONE the hue, k4 BUILD the light's
// warmth and shine, k5 SPACE the eye's height.

#define TH_MAX_STEPS 96
#define TH_FAR 24.0
#define TH_ZWRAP 64.0
#define TH_GROUT 0.03
#define TH_RISE 0.16

float gScale;
/** Cell columns per TH_ZWRAP world units: 64 or 96. */
float gPz;
float gHits;
float gEnv;
float gT;
float gHw;
float gCordY;
float gRiseGain;
float gLip;
float gWa;
float gWb;
float gW1;
float gW2;

// Set by map(): what the last lookup saw, for shading.
TruchetArc gArc;
float gCordHit;
float gHn;

/** A whole number of turns per gPz cells, as close to `f` rad/cell as that allows. */
float thFreq(float f) {
    return TRUCHET_TAU * max(1.0, floor(f * gPz / TRUCHET_TAU + 0.5)) / gPz;
}

/**
 * Chaos: lib_truchet's bend rebuilt so every term that varies along p.x (the
 * travel axis) is periodic in gPz; the others are the lib's own. Slope stays
 * under 1, so the plan never folds.
 */
vec2 thWarp(vec2 p) {
    if (gWa + gWb <= 1e-4) return p;
    p += gWa * vec2(sin(p.y * 0.31 + gT * 0.21), sin(p.x * gW1 - gT * 0.17 + 1.3));
    p += gWb * vec2(sin(p.y * 1.13 - gT * 0.37 + 2.1), sin(p.x * gW2 + gT * 0.33));
    return p;
}

/** The colour field, periodic along the travel axis like the bend. */
float thField(vec2 p) {
    float a = sin(p.x * thFreq(0.23) + 1.7 * sin(p.y * 0.17 + gT * 0.05) + gT * 0.03);
    float b = sin(p.y * 0.21 - p.x * thFreq(0.07) - gT * 0.04 + 1.9);
    return 0.5 * (a + b);
}

/**
 * The hex tile at plan point p (cell units), re-rolled by onsets, with the id
 * wrapped along the travel axis. rise = 0..1, how far off the floor a turning
 * tile is (a hump over the turn). hn = the hexagon norm, 0 at the centre and
 * 0.5 on the border.
 */
TruchetArc thHex(vec2 p, out float rise, out float hn) {
    vec4 c = truchetHexCell(p);
    vec2 id = vec2(mod(c.z, 2.0 * gPz), c.w);
    vec2 h = c.xy;
    hn = max(abs(h.x), max(abs(0.5 * h.x + 0.8660254 * h.y), abs(-0.5 * h.x + 0.8660254 * h.y)));
    vec3 roll = truchetRoll(id, gHits, gEnv, 3.0);
    float bitOld = step(0.5, roll.x);
    float bitNew = step(0.5, roll.y);
    rise = 0.0;
    if (bitOld == bitNew || roll.z >= 1.0) return truchetHexTile(h, bitNew);
    rise = sin(TRUCHET_PI * roll.z);
    float a = roll.z * TRUCHET_PI / 3.0;
    TruchetArc r = truchetHexTile(truchetRot(-a) * h, bitOld);
    r.n = truchetRot(a) * r.n;
    r.cap = mix(r.cap, 1.0 - r.cap, roll.z);
    return r;
}

vec2 planOf(vec2 xz) {
    return thWarp(vec2(xz.y, xz.x) * gScale);
}

float map(vec3 w) {
    float rise;
    float hn;
    TruchetArc a = thHex(planOf(w.xz), rise, hn);
    float lift = TH_RISE * gRiseGain * rise;
    float yc = w.y * gScale;
    // Tile top, bevelled into a shallow grout line at the border.
    float top = lift - TH_GROUT * smoothstep(0.4, 0.5, hn);
    // Height above a heightfield overstates the distance on its slope; the
    // bevel's steepest is TH_GROUT * 1.5 / 0.1 = 0.45 per plan unit (times the bend).
    float sl = 0.45 * gLip;
    float dG = (yc - top) * inversesqrt(1.0 + sl * sl);
    float dC = length(vec2(a.d / gLip, yc - (lift + gCordY))) - gHw;
    float d = min(dG, dC);
    // Never step past the hexagon's edge by more than half a cord radius.
    float border = (0.5 - hn) / gLip + 0.5 * gHw;
    d = min(d, max(border, 0.004));
    gArc = a;
    gCordHit = dC < dG ? 1.0 : 0.0;
    gHn = hn;
    return d / gScale;
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();
    float morph = truchetMorph();
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float mids = clamp(sndShaped(SND_MIDDLE), 0.0, 1.2);
    float bass = clamp(SND_BASS_PRESENCE, 0.0, 1.5);

    float k0 = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float k1 = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float k2 = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float k3 = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float k4 = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float k5 = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    gT = t;
    gHits = floor(SND_ONSET_COUNT);
    gEnv = SND_ONSET_ENV;
    // 1 or 1.5 cells per world unit: 64 * scale is a whole column count either way.
    gScale = morph >= 0.5 ? 1.5 : 1.0;
    gPz = TH_ZWRAP * gScale;
    gWa = 0.8 * clamp(uWarp, 0.0, 2.0);
    gWb = 0.18 * clamp(uTurbulence, 0.0, 2.0);
    gW1 = thFreq(0.29);
    gW2 = thFreq(1.07);
    // The bend's steepest slope, as lib_truchet bounds it (frequencies here
    // are rounded to within a few percent of the lib's).
    gLip = 1.0 + gWa * 0.33 + gWb * 1.15;
    gHw = (0.075 + 0.03 * mids * e) * (1.0 + 0.3 * k2);
    gCordY = gHw * (0.15 + 0.35 * bass * e);
    gRiseGain = clamp(e * (1.0 + 0.8 * k0), 0.0, 2.0);
    float slabTop = (TH_RISE * gRiseGain + gCordY + gHw + 0.02) / gScale;
    float slabBot = (TH_GROUT + 0.02) / gScale;

    float eyeH = 1.3 + 0.5 * k5;
    vec3 ro = vec3(uCamPos.x, eyeH, mod(uCamPos.z, TH_ZWRAP));
    // The rig's view, pitched down toward the floor.
    vec3 v = vec3(uv, uCamFocal);
    const float pitch = 0.5;
    v = vec3(v.x, v.y * cos(pitch) - v.z * sin(pitch), v.y * sin(pitch) + v.z * cos(pitch));
    vec3 rd = normalize(uCamBasis * v);

    float hue = 0.04 * sin(t * 0.025) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.12 * k3;
    vec3 fwd = normalize(vec3(uCamBasis[2].x, 0.0, uCamBasis[2].z) + vec3(0.0, 0.0, 1e-4));
    vec3 side = vec3(fwd.z, 0.0, -fwd.x);
    vec3 lightPos = ro + fwd * 3.4 + side * (1.6 * sin(t * 0.33)) + vec3(0.0, -0.6, 0.0);
    vec3 lightC = mix(vec3(1.0, 0.94, 0.85), pal(hue + 0.15), 0.3 - 0.25 * k4);

    vec3 sky = mix(pal(hue + 0.62) * 0.04, pal(hue + 0.75) * 0.13, smoothstep(-0.35, 0.35, rd.y));
    vec3 col = sky;

    float hitT = -1.0;
    float used = 0.0;
    if (rd.y < -0.002) {
        float tm = max((ro.y - slabTop) / -rd.y, 0.0);
        float t1 = min((ro.y + slabBot) / -rd.y, TH_FAR);
        for (int i = 0; i < TH_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (tm > t1) break;
            used = float(i);
            float d = map(ro + rd * tm);
            float eps = 0.0007 * tm + 0.0004;
            if (d < eps) {
                hitT = tm;
                break;
            }
            tm += max(d * 0.9, eps * 0.5);
        }
        // Below the grout line a ray must already be in the floor.
        if (hitT < 0.0 && tm >= t1 && t1 < TH_FAR) hitT = t1;
    }

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0012 * hitT + 0.0008);
        // Re-run the lookup at the hit itself, so the shading globals are its own.
        map(p);
        TruchetArc a = gArc;
        float cord = gCordHit;
        float hn = gHn;
        vec2 pp = planOf(p.xz);
        float field = thField(pp);
        // One pixel in cell units at this distance.
        float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y) * gScale * 1.5;

        vec3 toL = lightPos - p;
        float dl = length(toL);
        vec3 l = toL / dl;
        float atten = 1.0 / (1.0 + 0.1 * dl * dl);
        float dif = max(dot(n, l), 0.0);
        float spec = pow(max(dot(reflect(rd, n), l), 0.0), cord > 0.5 ? 36.0 : 14.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 0.8, 0.4, 1.0);

        vec3 albedo;
        float shine;
        if (cord > 0.5) {
            // Stripes along the cord: a whole number per arc, so they run on
            // unbroken into the next hex.
            float stripes = 2.0 + floor(morph * 2.0 + 0.5);
            float band = 0.5 + 0.5 * cos(TRUCHET_TAU * (a.s * stripes - uFlowTime * 0.35));
            float edge = 0.3 - 0.15 * k1;
            albedo = mix(pal(hue + 0.12 + 0.12 * field), pal(hue + 0.32 + 0.12 * field),
                         smoothstep(edge, 1.0 - edge, band)) * 0.9;
            shine = 0.55 + 0.5 * k4;
        } else {
            float tone = truchetTone(a, px);
            albedo = mix(pal(hue + 0.62) * 0.22, pal(hue + 0.75) * 0.45, tone);
            // Contact shadow hugging the cord, and the darker grout line.
            albedo *= 0.45 + 0.55 * smoothstep(gHw * gLip, gHw * gLip + 0.1, abs(a.d));
            albedo *= 1.0 - 0.55 * smoothstep(0.42, 0.5, hn);
            shine = 0.18;
        }
        col = albedo * (0.07 + 1.5 * dif * atten) * ao * lightC;
        col += lightC * spec * atten * shine * ao;
        // Sky fill from above, and a faint rim on the cords against the fog.
        col += albedo * sky * 0.5 * max(n.y, 0.0);
        if (cord > 0.5) col += sky * pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0) * 0.6;
        col = mix(col, sky, 1.0 - exp(-hitT * 0.11));
    }

    // The light itself, a soft glow hanging over the floor.
    vec3 toLight = lightPos - ro;
    float along = max(dot(toLight, rd), 0.0);
    float miss = length(toLight - rd * along);
    if (hitT < 0.0 || along < hitT) col += lightC * pal(hue + 0.15) * 0.07 / (1.0 + miss * miss * 60.0);

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
