#version 300 es
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
//#include lib_shell

// Neon City: a synthwave flyover. An endless grid of blocks below the eye has
// neon-edged roofs and lit windows, the streets are glowing grid lines, and a
// full moon hangs over a cold horizon straight down the travel axis. It is a
// NIGHT city on purpose: Grid and Outrun Drive already set a striped sunset
// on a neon horizon, and with a third one the three read as one picture.
//
// The camera is the shared rig (camFly). The block hash repeats every 1024
// world units along z, which divides the rig's 8192 wrap.
//
// ---- audio ------------------------------------------------------------------
//
// Each block is assigned one of the eight band regions (vec4(sndLevel(SND_SUB_ATT), sndSpectrum(0.23), sndShaped(SND_LOW), sndSpectrum(0.57)), vec4(sndLevel(SND_MID_HIGH_ATT), sndSpectrum(0.80), sndLevel(SND_TREBLE_ATT), sndLevel(SND_FLUX))) by
// its hash, and its height follows that band, so the skyline is a spectrum
// laid out at random. A snare (SND_SNARE_ENV) sends a ring of window light outward
// from under the eye through the blocks: it is a moving band, not a flash. A
// drop (SND_HELD_SURGE) stretches every building and lifts the moon and the horizon
// glow higher in the sky; they move, they do not brighten. Overall level
// rides SND_INTENSITY.
//
// ---- knobs ------------------------------------------------------------------
//
// Flame's, Opal's and Hollow's roles, each read as its offset from 0.5 so off
// is the tuned look: k0 PULSE the emission, k1 TEXTURE the neon edges' and
// street lines' width, k2 FLOW bends the avenues (a sideways shear of the whole
// city along z, period 64 so the 1024 tile holds), k3 TONE the palette
// position and the skyline (a high melody raises more towers out of the
// blocks, a low one cuts them down to a low-rise sprawl; no roof passes the
// tallest one the bound allows), k4 BUILD the horizon's glow, k5 SPACE the
// reach (far plane, haze).
//
// ---- shape inputs (lib_shell) ----------------------------------------------
//
// Every tunable of the city is a normalized input, mapped to its range in one
// place (the top of main) and read from globals. 0.5 is the look as tuned: the
// knobs rest there exactly while Note & key is off. Only knobs drive them here:
// this style leaves Morph, Warp and Turbulence to view(), which already bends
// its picture with them. Music reaches these only through the knobs (the
// reaction matrix); none reads the sound.
//
//   input       driver           lo .. rest .. hi
//   sLit        k0 PULSE         the share of windows lit 0.1 .. 0.22 .. 0.4
//   sRows       k1 TEXTURE       window rows per unit of height 1.4 .. 2.2 .. 3.4
//   gBend       k2 FLOW          the avenues' bend -1 .. 0 .. 1
//   gTone       k3 TONE          palette and skyline -0.3 .. 0 .. 0.3
//   gGlow       k4 BUILD         the horizon band's glow 0.5 .. 1 .. 1.5
//   sBlockH     k4 BUILD         the blocks' height 0.5 .. 1 .. 1.4
//   sFoot       k5 SPACE         a block's footprint 1.2 .. 1 .. 0.7
//   sMoonR      k5 SPACE         the moon's radius 0.12 .. 0.085 .. 0.06
//   reach       k5 SPACE         far plane and haze 0.8 .. 1 .. 1.2
//
// The cell (2), its hash wrap (512 cells) and the window columns (3 per unit,
// wrapping at 3072) stay constants: each is the 1024 tile the eye's rebase
// relies on. The roof bound gHiMax grows with sBlockH. The city goes through
// SHELL_FIELD3, so the shell's spawn, fractal, front and back hooks reach it.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Each cell measures only its own box. Below the tallest possible roof the
// distance is clamped to the cell boundary, so a taller neighbour is never
// stepped over. Every roof is capped NC_CLEAR below the eye, and a sphere
// around the eye is carved out as well. FLOW's shear slopes at most
// 1 x 2pi / 64 = 0.098, which stretches a distance by under 0.5 %, inside the
// 0.95 step.

#define NC_MAX_STEPS 96
#define NC_FAR 80.0
#define NC_GROUND -9.0
#define NC_CELL 2.0
/** Cells along z before the hash repeats: 1024 world units. */
#define NC_ZWRAP 512.0
/** Window columns along z repeat every 3072, also 1024 world units. */
#define NC_WINWRAP 3072.0
/** Tallest a building can be before the drop stretch. */
#define NC_MAX_HEIGHT 7.1
#define NC_CLEAR 0.9
#define NC_BUBBLE 0.8

float gStretch;
float gCap;
float gHiMax;

float gTop;
float gBand;
float gH;
vec2 gCentre;
vec2 gHalf;
float gMat;
/** k4 BUILD: the horizon band's glow, 0.5..1.5 (1 at rest). */
float gGlow = 1.0;
/** The eye, z rebased into 0..1024 (see main). */
vec3 gEye;
/** k2 FLOW: how far the avenues bend sideways, world units (0 at rest); k3 TONE's palette offset. */
float gBend = 0.0;
float gTone = 0.0;
/** Shape inputs (see the header). */
float sLit;
float sRows;
float sBlockH;
float sFoot;
float sMoonR;

/** The city's own frame: the world sheared sideways along z by FLOW's bend. */
vec3 cityP(vec3 p) {
    return vec3(p.x - gBend * sin(p.z * (6.2831853 / 64.0)), p.y, p.z);
}

/** One of the eight band regions, picked by h in 0..1. */
float bandPick(float h) {
    float k = floor(h * 8.0);
    vec4 lo = vec4(equal(vec4(k), vec4(0.0, 1.0, 2.0, 3.0)));
    vec4 hi = vec4(equal(vec4(k), vec4(4.0, 5.0, 6.0, 7.0)));
    return dot(lo, vec4(sndLevel(SND_SUB_ATT), sndSpectrum(0.23), sndShaped(SND_LOW), sndSpectrum(0.57))) + dot(hi, vec4(sndLevel(SND_MID_HIGH_ATT), sndSpectrum(0.80), sndLevel(SND_TREBLE_ATT), sndLevel(SND_FLUX)));
}

float mapField(vec3 w) {
    vec3 p = cityP(w);
    vec2 cell = floor(p.xz / NC_CELL);
    vec2 centre = (cell + 0.5) * NC_CELL;
    vec2 f = p.xz - centre;
    uint u = uhash3(uvec3(ivec3(int(cell.x), 7, int(mod(cell.y, NC_ZWRAP)))));
    float h = uhashUnit(u);
    uint u2 = uhash(u);
    float hb = uhashUnit(u2);
    float hw = uhashUnit(uhash(u2));

    float band = clamp(bandPick(hb), 0.0, 1.0);
    // TONE: more towers for a high melody (each fades in on its own hash), shorter for a low one.
    float tower = step(0.88, h);
    if (gTone > 0.0) tower = max(tower, smoothstep(0.88 - 1.2 * gTone, 0.92 - 1.2 * gTone, h));
    tower *= 1.0 + 2.6 * min(gTone, 0.0);
    float height = ((0.8 + 3.2 * h) * (0.45 + 0.7 * band) * sBlockH + 2.5 * tower) * gStretch;
    float top = min(NC_GROUND + height, gCap);
    // Footprint at most 0.8 of the 1.0 half-cell (0.96 at SPACE's widest): the rest is street.
    vec2 hs = vec2(0.55 + 0.25 * hw, 0.55 + 0.25 * fract(hw * 7.13)) * sFoot;
    // The box runs one unit below the ground so there is no seam at its foot.
    float yc = 0.5 * (top + NC_GROUND - 1.0);
    float hy = 0.5 * (top - NC_GROUND + 1.0);
    float db = sdBox(vec3(f.x, p.y - yc, f.y), vec3(hs.x, hy, hs.y));

    vec2 af = abs(f);
    float toEdge = NC_CELL * 0.5 - max(af.x, af.y);
    float d = min(db, max(toEdge + 0.02, p.y - gHiMax));
    float dg = p.y - NC_GROUND;
    gMat = dg < db ? 1.0 : 0.0;
    d = min(d, dg);
    d = max(d, NC_BUBBLE - length(w - gEye));

    gTop = top;
    gBand = band;
    gH = h;
    gCentre = centre;
    gHalf = hs;
    return d;
}

/** The city as the shell sees it: spawn, fractal, front and back route through here (identity at rest). */
float map(vec3 p) {
    return SHELL_FIELD3(p, mapField);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** Night sky: a cold gradient, a thin glow band at glowY (in rd.y) and the moon above it. */
vec3 skyColour(vec3 rd, float glowY) {
    float y = rd.y;
    // The gradient itself stays put, so a drop moves the glow without
    // brightening the sky as a whole. Deep blue night, the palette only tints.
    vec3 night = mix(vec3(0.03, 0.05, 0.12), pal(0.6 + gTone) * 0.08, 0.35);
    vec3 col = mix(night * 2.2, night * 0.4, smoothstep(-0.03, 0.57, y));
    float by = (y - glowY) / 0.07;
    col += mix(pal(0.55 + gTone), vec3(0.4, 0.6, 1.0), 0.5) * 0.18 * exp(-by * by) * gGlow;

    // The moon: pale, unstriped, a little above the glow, mottled with maria.
    vec3 md = normalize(vec3(0.12, glowY + 0.26, 1.0));
    float r = sqrt(max(2.0 * (1.0 - dot(rd, md)), 0.0));
    float disc = 1.0 - smoothstep(sMoonR, sMoonR + 0.005, r);
    vec2 mq = (rd.xy - md.xy) * 24.0;
    float maria = 0.6 + 0.4 * sin(mq.x * 1.3 + 1.3) * sin(mq.y * 1.6 - 0.4);
    col += mix(vec3(0.95, 0.96, 1.0), pal(0.6), 0.12) * disc * (0.72 + 0.22 * maria);
    col += vec3(0.5, 0.6, 0.9) * 0.12 * (1.0 - smoothstep(0.1, 0.5, r));

    vec3 sp = rd * 150.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.99, s);
    col += vec3(0.7) * star * smoothstep(glowY + 0.15, glowY + 0.4, y);
    return col;
}

void main() {
    // ---- shape inputs: each normalized input mapped to its range, here only (see the header).
    sLit = shellMap(shellKnob(0), 0.1, 0.22, 0.4);
    sRows = shellMap(shellKnob(1), 1.4, 2.2, 3.4);
    gBend = shellMap(shellKnob(2), -1.0, 0.0, 1.0);
    gTone = shellMap(shellKnob(3), -0.3, 0.0, 0.3);
    gGlow = shellMap(shellKnob(4), 0.5, 1.0, 1.5);
    sBlockH = shellMap(shellKnob(4), 0.5, 1.0, 1.4);
    sFoot = shellMap(shellKnob(5), 1.2, 1.0, 0.7);
    sMoonR = shellMap(shellKnob(5), 0.12, 0.085, 0.06);
    float reach = shellMap(shellKnob(5), 0.8, 1.0, 1.2);

    vec2 uv = view();
    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024, so this shift is
    // invisible and exact (power of two). Without it, far down the 8192
    // travel a highp z has ~0.001 of fraction left: every sample and the
    // finite-difference normals quantise into speckle.
    ro.z = mod(ro.z, NC_ZWRAP * NC_CELL);
    gEye = ro;

    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float energy = clamp(((SND_BASS_PRESENCE + SND_MID_PRESENCE + SND_TREBLE_PRESENCE) / 3.0), 0.0, 1.5);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);

    gStretch = 1.0 + 0.55 * drop;
    gCap = uCamPos.y - NC_CLEAR;
    gHiMax = min(NC_GROUND + NC_MAX_HEIGHT * gStretch * max(sBlockH, 1.0), gCap);
    float glowY = 0.02 + 0.16 * drop;

    float t = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < NC_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        // Above every roof and climbing: nothing left to hit.
        if (rd.y >= 0.0 && p.y > gHiMax + 0.1) break;
        float d = map(p);
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.95;
        if (t > NC_FAR * reach) break;
    }

    vec3 sky = skyColour(rd, glowY);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 pw = ro + rd * hitT;
        vec3 n = normalAt(pw, max(0.0015, 0.001 * hitT));
        map(pw);
        // The streets, edges and windows are drawn in the city's own (bent) frame.
        vec3 p = cityP(pw);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        vec3 horizon = pal(0.92 + gTone) * 0.30;
        float hue = 0.55 + 0.35 * gH + gTone;

        if (gMat > 0.5) {
            // Streets: neon grid lines on the cell boundaries.
            vec2 g = abs(fract(p.xz / NC_CELL + 0.5) - 0.5) * NC_CELL;
            float w = (0.03 + 0.0025 * hitT) * (1.0 + kTexture);
            float line = (1.0 - smoothstep(0.0, w, min(g.x, g.y)));
            col = pal(0.70 + gTone) * 0.02 + pal(0.85 + gTone) * line * 0.8 + horizon * fres * 0.3;
        } else {
            vec3 L = normalize(vec3(0.3, 0.7, -0.6));
            vec3 facade = pal(hue + 0.6) * (0.035 + 0.05 * max(dot(n, L), 0.0)) + horizon * fres * 0.25;

            // Box edges are where two face distances are both near zero.
            vec3 lp = p - vec3(gCentre.x, 0.0, gCentre.y);
            float ex = gHalf.x - abs(lp.x);
            float ez = gHalf.y - abs(lp.z);
            float et = gTop - p.y;
            float second = max(min(ex, ez), min(max(ex, ez), et));
            float ew = (0.025 + 0.002 * hitT) * (1.0 + kTexture);
            float edge = (1.0 - smoothstep(0.0, ew, second));

            // Windows on the side faces.
            float sideFace = 1.0 - step(0.5, n.y);
            float along = abs(n.x) > abs(n.z) ? p.z : p.x;
            vec2 wc = vec2(along * 3.0, p.y * sRows);
            vec2 wf = fract(wc);
            float shape = smoothstep(0.18, 0.26, wf.x) * (1.0 - smoothstep(0.74, 0.82, wf.x))
                        * smoothstep(0.25, 0.33, wf.y) * (1.0 - smoothstep(0.77, 0.85, wf.y));
            float wid = hashCell(ivec3(int(mod(floor(wc.x), NC_WINWRAP)), int(floor(wc.y)), int(gH * 4096.0)));
            float lit = step(wid, sLit + 0.35 * gBand);
            // A slow sweep keeps the lights moving in silence.
            float dist = length(pw.xz - gEye.xz);
            float sweep = 0.5 + 0.5 * sin(dist * 0.35 - uTime * 1.2 + gH * 6.0);
            // The snare ring: it starts under the eye and runs outward as the
            // envelope falls, lighting nearly every window it passes.
            float front = 3.0 + 42.0 * (1.0 - snare);
            float dr = (dist - front) / 3.5;
            float wave = snare * exp(-dr * dr) * step(wid, 0.9);
            vec3 win = pal(hue + 0.12) * lit * (0.45 + 0.45 * sweep)
                     + mix(pal(hue + 0.4), vec3(1.0), 0.35) * wave * 1.3;

            col = facade + win * shape * sideFace + pal(hue) * edge * 1.1;
        }
        col = mix(col, sky, 1.0 - exp(-hitT * 0.028 / reach));
    }

    col *= 0.80 + 0.30 * swell + 0.08 * energy;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col * (1.0 + 0.5 * kPulse)), 1.0);
}
