#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the traces (below), not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Scope: a flight down the inside of an oscilloscope tube. The old flat style
// was the raw waveform as one glowing line over a graticule; here the
// graticule is the tube itself, a long wide corridor of dark phosphor glass
// with its grid lines glowing on the walls, floor and ceiling, and the
// waveform is drawn again and again as glowing traces on transparent frames
// standing across the corridor every two units. The eye flies through them:
// each frame is the live waveform, so the corridor ahead is a stack of the
// sound's shape, the near ones sharp, the far ones lost in the tube's glow.
// Each frame lights a band of glass where it meets the walls.
//
// The camera is the shared rig (camFly). The corridor runs down +z around the
// rig's path; every z term has a period dividing 64 (graticule 0.5 and 2,
// frames 2, the walls' bulge 32, the helix 32), so rebasing the eye by 64
// (power of two, divides the rig's 8192 wrap) is invisible and keeps z precise.
//
// ---- audio ------------------------------------------------------------------
//
// Waveform (sndWave01)          the shape of every trace: the style's identity.
// Volume (SND_VOL_ATT)          the traces' height, and where they sit on the
//                               palette, as the flat scope did.
// Kick (SND_KICK_ENV)           the tube pinches just ahead of the eye: the side
//                               walls squeeze in. Geometry, nothing brightens.
// Snare (SND_SNARE_ENV)         the beam's halo widens and lifts.
// Hat (SND_HAT_ENV)             the graticule's major crossings twinkle.
// Treble (SND_TREBLE_ATT)       the graticule glows brighter.
// Pitch (SND_HELD_NOTE_CHANGE)  slides the hue of the whole tube.
// SND_ENERGY_TREND              the overall level, as everywhere.
//
// Controls: Morph bends each trace from the flat line into a ring scope (the
// waveform as a radius around the corridor's axis, mirrored so it has no
// seam); Warp twists the frames about the axis into a slow helix; Turbulence
// jitters the beam. Knobs: k0 PULSE the kick's pinch, k1 TEXTURE the
// graticule, k2 FLOW the helix, k3 TONE the hue, k4 BUILD the halo, k5 SPACE
// the tube's width. Each is read as its offset from rest (0.5), so with Note &
// key reactions off the style is exactly its tuned look.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Only the walls are marched. Floor and ceiling are flat planes (exact). The
// side walls are |x| = W(z), a bulge plus the kick's pinch; W - |x| has a
// gradient of length sqrt(1 + W'^2), and W' is bounded by the two amplitudes
// times their rates, so dividing by that bound is a true lower bound. Nothing
// can be hit before the ray leaves the box shrunk by the walls' largest inward
// excursion, so that exit is found analytically and the march starts there:
// a grazing ray down the corridor costs a handful of steps, not the budget. A
// sphere around the eye is carved out of everything, so the eye is never in a
// wall even where the rig's path swings wide. The traces are not marched at
// all: each frame is a plane met in closed form, and the line's distance is
// corrected by its own slope, so a steep waveform does not thin out.

#define SC_MAX_STEPS 48
#define SC_FAR 52.0
#define SC_TAU 6.2831853
#define SC_PI 3.1415927
/** Every z term tiles with a period dividing this (power of two, divides 8192). */
#define SC_ZWRAP 64.0
/** Half-width and half-height of the tube at rest. */
#define SC_W 5.0
#define SC_H 2.6
/** The side walls' slow bulge, world units, period 32. */
#define SC_BULGE 0.3
#define SC_BULGE_K (SC_TAU / 32.0)
/** The kick's pinch: how far ahead of the eye it centres, and how fast it falls off. */
#define SC_PINCH_AHEAD 7.0
#define SC_PINCH_FALL 0.3
/** Frames: spacing (divides 64) and how many ahead are drawn at most. */
#define SC_FRAME_SP 2.0
#define SC_FRAMES 9
/** Half-width of a flat trace across the tube. */
#define SC_TRACE_W 3.6
#define SC_BUBBLE 0.6

float gW0;
float gPinch;
float gLip;
vec3 gEye;

/** Style knob i's offset from rest: -0.5..0.5, 0 while nothing drives the knobs. */
float scKnob(int i) { return clamp(uKnob[i], 0.0, 1.0) - 0.5; }

/** Half-width of the tube at depth z. */
float wallW(float z) {
    return gW0 + SC_BULGE * sin(z * SC_BULGE_K) - gPinch * exp(-abs(z - gEye.z - SC_PINCH_AHEAD) * SC_PINCH_FALL);
}

/** dW/dz, for the side walls' normal. */
float wallWp(float z) {
    float c = z - gEye.z - SC_PINCH_AHEAD;
    return SC_BULGE * SC_BULGE_K * cos(z * SC_BULGE_K)
         + gPinch * SC_PINCH_FALL * exp(-abs(c) * SC_PINCH_FALL) * sign(c);
}

float map(vec3 p) {
    float dx = (wallW(p.z) - abs(p.x)) / gLip;
    float dy = SC_H - abs(p.y);
    float d = min(dx, dy);
    return max(d, SC_BUBBLE - length(p - gEye));
}

/** A graticule of lines every `s` world units, `w` wide, on the wall coordinates g. */
float graticule(vec2 g, float s, float w) {
    vec2 f = abs(fract(g / s + 0.5) - 0.5) * s;
    vec2 l = 1.0 - smoothstep(0.0, w, f);
    return max(l.x, l.y);
}

/** The flat trace's height at u (0..1 across the tube) on the frame at wrapped depth zw. */
float traceY(float u, float zw, float amp) {
    float y = (sndWave01(u) * 2.0 - 1.0) * amp * 0.9;
    return y + uTurbulence * 0.12 * sin(u * 53.0 + uTime * 6.0 + zw * 1.7);
}

void main() {
    vec2 uv = view();

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float energy = clamp(sndShaped(SND_LEVEL), 0.0, 1.5);
    float vol = sndLevel(SND_VOL_ATT);
    float amp = 0.5 + vol;
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float treble = clamp(sndLevel(SND_TREBLE_ATT), 0.0, 1.5);
    float hue = 0.22 * clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0) + 0.25 * scKnob(3);

    gW0 = SC_W * (1.0 + 0.3 * scKnob(5));
    gPinch = 0.8 * kick * clamp(1.0 + 1.2 * scKnob(0), 0.0, 2.0);
    gLip = sqrt(1.0 + pow(SC_BULGE * SC_BULGE_K + gPinch * SC_PINCH_FALL, 2.0));

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64, so this shift is exact
    // and invisible, and z keeps its fraction far down the rig's travel.
    ro.z = mod(ro.z, SC_ZWRAP);
    gEye = ro;

    // Skip the open box: nothing is solid inside the tube shrunk by the side
    // walls' deepest excursion, so the march starts where the ray leaves it.
    float wMin = gW0 - SC_BULGE - gPinch;
    float t = 0.02;
    if (abs(ro.x) < wMin && abs(ro.y) < SC_H) {
        float tx = abs(rd.x) > 1e-4 ? (sign(rd.x) * wMin - ro.x) / rd.x : 1e4;
        float ty = abs(rd.y) > 1e-4 ? (sign(rd.y) * SC_H - ro.y) / rd.y : 1e4;
        t = max(min(tx, ty) - 0.01, 0.02);
    }

    float hitT = -1.0;
    if (t < SC_FAR) {
        for (int i = 0; i < SC_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            float d = map(ro + rd * t);
            float eps = 0.0008 * t + 0.0008;
            if (d < eps) {
                hitT = t;
                break;
            }
            t += max(d * 0.95, eps);
            if (t > SC_FAR) break;
        }
    }

    // The far end of the tube: the glow down the travel axis, both ways.
    float fogK = 0.045;
    float ahead = pow(abs(rd.z), 12.0);
    vec3 fogCol = pal(hue + 0.55) * (0.03 + 0.22 * ahead) + vec3(0.002, 0.004, 0.003);
    vec3 col = fogCol;
    float sceneT = hitT > 0.0 ? hitT : SC_FAR;
    float pxK = 2.0 / (max(uCamFocal, 0.1) * uResolution.y);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        float dx = (wallW(p.z) - abs(p.x)) / gLip;
        float dy = SC_H - abs(p.y);
        bool side = dx < dy;
        vec3 n = side ? normalize(vec3(-sign(p.x), 0.0, wallWp(p.z))) : vec3(0.0, -sign(p.y), 0.0);
        // Corners darken: the distance to the other wall stands in for AO.
        float ao = 0.45 + 0.55 * smoothstep(0.0, 1.2, side ? dy : dx);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);

        // The graticule, on the wall's own coordinates; major lines every 2
        // along z meet the frames. Fine lines fade before they alias.
        vec2 g = vec2(p.z, side ? p.y : p.x);
        float px = hitT * pxK;
        float lw = max(0.012, px * 1.2);
        float minor = graticule(g, 0.5, lw) * (1.0 - smoothstep(0.02, 0.09, px));
        float major = graticule(g, 2.0, lw * 1.6);
        vec2 fm = abs(fract(g / 2.0 + 0.5) - 0.5) * 2.0;
        float node = (1.0 - smoothstep(0.0, lw * 3.0, fm.x)) * (1.0 - smoothstep(0.0, lw * 3.0, fm.y));
        float gratGain = (0.35 + 0.45 * treble) * clamp(1.0 + 1.5 * scKnob(1), 0.1, 2.5);

        // Each frame lights the glass where it meets the walls.
        float band = exp(-abs(p.z - round(p.z / SC_FRAME_SP) * SC_FRAME_SP) * 2.5);

        vec3 glass = pal(hue + 0.55) * (0.025 + 0.06 * ndv) * ao + fogCol * fres * 0.8;
        vec3 phos = pal(hue + 0.3);
        col = glass
            + phos * (0.18 * minor + 0.55 * major) * gratGain * ao
            + mix(phos, vec3(1.0), 0.5) * node * hat * 0.9 * gratGain
            + pal(hue + 0.1 + vol * 0.3) * band * amp * 0.07 * ao;
        col = mix(col, fogCol, 1.0 - exp(-hitT * fogK));
    }

    // The traces: one plane per frame, met in closed form, the live waveform
    // drawn on each. Frames stand at fixed world z (index wrapped by 64), so
    // they hold still under the rebase and the parallax is true.
    if (abs(rd.z) > 0.05) {
        float dir = rd.z > 0.0 ? 1.0 : -1.0;
        float base = rd.z > 0.0 ? floor(ro.z / SC_FRAME_SP) + 1.0 : ceil(ro.z / SC_FRAME_SP) - 1.0;
        int frames = uSteps < 80.0 ? 6 : SC_FRAMES;
        float morph = clamp(uMorph, 0.0, 1.0);
        float helix = clamp(uWarp, 0.0, 2.0) * 0.9 + 0.8 * scKnob(2);
        float haloK = (0.30 + 0.55 * snare) * clamp(1.0 + 1.5 * scKnob(4), 0.2, 2.5);
        for (int i = 0; i < SC_FRAMES; i++) {
            if (i >= frames) break;
            float zi = (base + dir * float(i)) * SC_FRAME_SP;
            float ti = (zi - ro.z) / rd.z;
            if (ti < 0.2 || ti > sceneT) continue;
            float zw = mod(zi, SC_ZWRAP);
            // The helix: a sine of depth, so any amplitude survives the wrap.
            vec2 q = rot2(helix * sin(zw * (SC_TAU / 32.0) + uTime * 0.15)) * (ro.xy + rd.xy * ti);

            float px = ti * pxK;
            float d = 1e3;
            float u = 0.5;
            float ends = 1.0;
            if (morph < 0.999) {
                // The flat scope: y = wave(x), its distance corrected by the slope.
                float ul = q.x / (2.0 * SC_TRACE_W) + 0.5;
                float du = 1.0 / 512.0;
                float f0 = traceY(ul, zw, amp);
                float f1 = traceY(ul + du, zw, amp);
                float slope = (f1 - f0) / (2.0 * SC_TRACE_W * du);
                float dl = abs(q.y - f0) / sqrt(1.0 + slope * slope);
                float el = smoothstep(0.0, 0.03, ul) * (1.0 - smoothstep(0.97, 1.0, ul));
                d = dl;
                u = ul;
                ends = el;
            }
            if (morph > 0.001) {
                // The ring scope: the wave as a radius, mirrored top to bottom
                // so the two ends of the window meet without a seam.
                float r = length(q);
                float a = r > 1e-4 ? atan(q.y, q.x) : 0.0; // atan(0, 0) is undefined in GLSL ES
                float ur = abs(a) / SC_PI;
                float fr = 1.4 + 0.55 * (traceY(ur, zw, amp) / 0.9);
                float dr = abs(r - fr);
                bool pure = morph >= 0.999;
                d = pure ? dr : mix(d, dr, morph);
                u = pure ? ur : mix(u, ur, morph);
                ends = pure ? 1.0 : mix(ends, 1.0, morph);
            }

            float core = max(0.012, px * 1.1);
            float halo = core * 6.0 + 0.06 + 0.1 * snare;
            float beam = exp(-d / core) + haloK * exp(-d / halo);
            float fade = smoothstep(0.2, 1.6, ti) * exp(-ti * fogK) * ends;
            col += pal(hue + u * 0.5 + vol * 0.3) * beam * fade * 0.9;
        }
    }

    col *= 0.8 + 0.28 * swell + 0.1 * energy;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
