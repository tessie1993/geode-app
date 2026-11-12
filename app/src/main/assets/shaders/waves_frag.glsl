#version 300 es
// The shape controls (Morph, Warp, Turbulence) are spent on the ribbons below,
// not on the screen, so view() must not bend the rays a second time.
#define VIEW_OWNS_SHAPE
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Waves: the five oscilloscope ribbons, now laid out as lanes of light in a
// dark hall and flown along. Each ribbon is a flat glowing band running down
// +z whose height follows the live waveform: the trace is read along the
// ribbon's length, so the eye travels beside the music's own shape instead of
// watching it on a flat screen. The lanes sit side by side in a shallow arc
// below the eye (the centre lane lowest), over a black glass floor that
// catches their light, and every band gives off a soft halo through the fog,
// which is the old shader's exp(-d) line glow taken into the volume.
//
// The camera is the shared rig (camFly). Every z term has a period dividing
// 64 (the trace's ping-pong read 64, the lateral warp 32, the twist 16, the
// ripple 8, the floor lines 4), so the eye is rebased by mod(z, 64): exact,
// invisible under the rig's 8192 wrap, and it keeps highp z precise.
//
// ---- audio ------------------------------------------------------------------
//
// Waveform (sndWave01)          each ribbon's height along its length: the trace.
// Volume (SND_VOL)              the trace's amplitude, as the 2D style had it.
// Kick (SND_KICK_ENV)           a swell that runs away down the ribbons from the
//                               eye: the amplitude lifts in a moving band. Geometry,
//                               with only a little light in the band. k0 PULSE scales it.
// Bass (SND_BASS_ATT)           the bands widen.
// Treble (SND_TREBLE_ATT)       glints along the ribbons' edges twinkle up.
// Pitch (SND_HELD_NOTE_CHANGE)  slides the hue. k3 TONE offsets it.
// SND_ENERGY_TREND              overall level, as everywhere.
// Mids (SND_MIDDLE)             turn the eye about the vertical while they climb,
//                               easing back as they fall, as lib_flame's
//                               gFlameTurn turns its structure (0.6 x sndShaped x
//                               SND_DRIVE, 0.9 rad at most, 0 in silence); the
//                               lanes hold still. Mid gain scales it.
// k1 TEXTURE adds to the fast ripple (Turbulence), k2 FLOW the halo, k4 BUILD
// the amplitude, k5 SPACE the lane spacing. Morph twists the bands about
// their length, Warp bends the lanes sideways, Turbulence adds a fast ripple.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Only the nearest lane is measured. Every other lane is at least as far as
// its neighbour's slab across x (spacing -+ u, minus the band's half width),
// a bound with only the warp's slope in its gradient, which is min'd in. The
// nearest band is a rounded box in a cross-section that slides with z, so its
// gradient is bounded by sqrt(1 + s^2), where s is the cross-section's z slope:
// the lateral warp's, the ripple's and the kick swell's analytic slopes, the
// twist's times the cross-section radius, and the trace's own slope, measured
// from the same two texels the height reads (linear between texels, so the
// local slope is the slope). The distance is divided by that. A sphere around
// the eye is carved out of everything, so the eye never sits inside a band.

#define WV_MAX_STEPS 88
#define WV_FAR 44.0
#define WV_TAU 6.2831853
#define WV_ZWRAP 64.0
/** Height of the centre lane's rest line; the outer lanes rise on an arc. */
#define WV_BASE -2.7
#define WV_FLOOR -4.6
/** Half thickness of a band. */
#define WV_TH 0.035
#define WV_BUBBLE 0.7
/** uSoundTex width (core/viz/SoundUniforms kTexWidth): one texel of the trace. */
#define WV_TEXEL (1.0 / 128.0)

float gSpace;
float gHalfW;
float gAmp;
float gKick;
float gPulseZ;
float gTurb;
float gRoll;
float gWarp;
vec3 gEye;

/** Set by map(): the nearest band's distance, lane (-2..2), trace position and cross-section. */
float gRib;
float gLane;
float gS;
vec2 gQ;
float gMat;

/** A continuous ping-pong, 0..1..0 over one unit: the trace is read forward then back, never torn. */
float tri(float x) {
    return abs(fract(x) * 2.0 - 1.0);
}

float box2(vec2 q, vec2 b) {
    vec2 d = abs(q) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float map(vec3 p) {
    // The lanes bend sideways together (Warp).
    float zw = p.z * (WV_TAU / 32.0);
    float xo = gWarp * sin(zw);
    float xoSlope = abs(gWarp * (WV_TAU / 32.0) * cos(zw));
    float lx = p.x - xo;

    float fi = clamp(floor(lx / gSpace + 2.5), 0.0, 4.0);
    float lane = fi - 2.0;
    float u = lx - lane * gSpace;
    float baseY = WV_BASE + 0.35 * lane * lane;

    // The trace, read along the band: two adjacent texels give the height
    // (their mean) and the local slope (their difference).
    float s = tri(p.z * (1.0 / WV_ZWRAP) + fi * 0.13 + uTime * 0.02);
    float w0 = sndWave01(s);
    float w1 = sndWave01(s + WV_TEXEL);
    float h = w0 + w1 - 1.0;
    // dh/ds = 2 (w1 - w0) / texel; ds/dz = 2 / 64.
    float hSlope = abs(w1 - w0) * (2.0 / WV_TEXEL) * (2.0 / WV_ZWRAP);

    // The kick swell, a band of lift running away from the eye.
    float dz = p.z - gPulseZ;
    float pulse = gKick * exp(-abs(dz) * 0.3);
    float amp = gAmp * (1.0 + 0.6 * pulse);
    float zr = p.z * (WV_TAU / 8.0) + uTime * 2.0 + fi * 1.7;
    float rip = gTurb * sin(zr);
    float cy = baseY + amp * h + rip;
    float cySlope = amp * hSlope + gAmp * 0.6 * 0.3 * pulse + abs(gTurb * (WV_TAU / 8.0) * cos(zr));

    // The twist about the band's length (Morph).
    float zt = p.z * (WV_TAU / 16.0) + fi * 1.3;
    float roll = gRoll * sin(zt);
    float rollSlope = abs(gRoll * (WV_TAU / 16.0) * cos(zt));
    vec2 q = rot2(roll) * vec2(u, p.y - cy);
    float db = box2(q, vec2(gHalfW, WV_TH)) - 0.015;
    float zs = xoSlope + cySlope + rollSlope * length(q);
    float dRib = db / sqrt(1.0 + zs * zs);

    // The next lane on each side, if there is one: past an end lane the point
    // is farther from the others, never inside them.
    float toRight = fi < 3.5 ? gSpace - u : 1e3;
    float toLeft = fi > 0.5 ? gSpace + u : 1e3;
    float dOther = (min(toRight, toLeft) - (gHalfW + WV_TH + 0.015)) / sqrt(1.0 + xoSlope * xoSlope);
    float dFloor = p.y - WV_FLOOR;

    gRib = db;
    gLane = lane;
    gS = s;
    gQ = q;
    gMat = dFloor < dRib ? 1.0 : 0.0;

    float d = min(min(dRib, dOther), dFloor);
    return max(d, WV_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 skyColour(vec3 rd, float hue) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(hue + 0.5) * 0.06, pal(hue + 0.65) * 0.015, smoothstep(-0.1, 0.6, up));
    // The vanishing point of the lanes, straight down the travel axis.
    float ahead = max(rd.z, 0.0);
    col += pal(hue + 0.1) * (0.20 * pow(ahead, 14.0) + 0.04 * ahead * ahead);
    return col;
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The mids turn the eye about the vertical (see the header).
    rd.xz = rot2(0.6 * sndShaped(SND_MIDDLE) * clamp(SND_DRIVE, 0.0, 1.0)) * rd.xz;
    ro.z = mod(ro.z, WV_ZWRAP);
    gEye = ro;

    float k0 = uKnob[0] - 0.5;
    float k1 = uKnob[1] - 0.5;
    float k2 = uKnob[2] - 0.5;
    float k3 = uKnob[3] - 0.5;
    float k4 = uKnob[4] - 0.5;
    float k5 = uKnob[5] - 0.5;

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float vol = clamp(sndLevel(SND_VOL), 0.0, 1.5);
    float bass = clamp(sndLevel(SND_BASS_ATT), 0.0, 1.5);
    float treble = clamp(sndLevel(SND_TREBLE_ATT), 0.0, 1.0);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);

    gSpace = 2.6 * (1.0 + 0.5 * k5);
    gHalfW = 0.5 + 0.22 * bass;
    gAmp = (0.35 + 0.65 * vol) * (1.0 + 0.6 * k4);
    gKick = kick * clamp(1.0 + 1.6 * k0, 0.0, 2.0);
    // As the envelope falls the swell runs out ahead of the eye.
    gPulseZ = ro.z + 2.0 + 30.0 * (1.0 - kick);
    gTurb = clamp(uTurbulence, 0.0, 2.0) * 0.3 + max(k1, 0.0) * 0.3;
    gRoll = clamp(uMorph, 0.0, 2.0) * 0.9;
    gWarp = clamp(uWarp, 0.0, 2.0) * 1.4;

    float hue = 0.22 * clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0) + 0.3 * k3;

    float t = 0.05;
    float hitT = -1.0;
    // The halo: the bands' light integrated along the ray (weight times step
    // length, so the step count does not change it), with its mean palette
    // position so it can be coloured once at the end.
    float glow = 0.0;
    float glowPos = 0.0;
    for (int i = 0; i < WV_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        if (d < 0.0012 + 0.0008 * t) {
            hitT = t;
            break;
        }
        float stepLen = max(d * 0.85, 0.002 + 0.001 * t);
        float wgt = exp(-max(gRib, 0.0) * 5.0) * stepLen;
        glow += wgt;
        glowPos += wgt * ((gLane + 2.0) * 0.2 + gS * 0.35);
        t += stepLen;
        if (t > WV_FAR) break;
    }

    vec3 sky = skyColour(rd, hue);
    vec3 col = sky;
    float fogK = 0.045;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        // Restore the hit's globals after the normal taps.
        map(p);
        vec3 L = normalize(vec3(-0.3, 0.9, 0.35));
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 40.0);
        float lanePos = (gLane + 2.0) * 0.2;

        if (gMat > 0.5) {
            // Black glass: a sheen of each lane's light directly below it, and
            // faint lines along the floor so the flight reads at speed.
            float lx = p.x - gWarp * sin(p.z * (WV_TAU / 32.0));
            float fl = clamp(floor(lx / gSpace + 2.5), 0.0, 4.0);
            float u = lx - (fl - 2.0) * gSpace;
            float sheen = exp(-abs(u) * 1.3) * (0.5 + 0.5 * fres);
            vec3 laneCol = pal(hue + (fl * 0.2) + 0.35 * tri(p.z * (1.0 / WV_ZWRAP) + fl * 0.13 + uTime * 0.02));
            float lines = 1.0 - smoothstep(0.0, 0.04 + 0.002 * hitT, abs(fract(p.z * 0.25) - 0.5) - 0.46);
            col = vec3(0.004, 0.004, 0.008) + laneCol * sheen * (0.10 + 0.08 * swell)
                + pal(hue + 0.6) * lines * 0.025 * exp(-abs(u) * 0.6) + vec3(spec) * 0.05;
        } else {
            vec3 base = pal(hue + lanePos + gS * 0.35);
            // The long edges are the trace lines of the old scope: brightest.
            float edge = smoothstep(gHalfW - 0.14, gHalfW, abs(gQ.x));
            float dif = max(dot(n, L), 0.0);
            float pulseLight = gKick * exp(-abs(p.z - gPulseZ) * 0.3);
            col = base * (0.18 + 0.30 * dif + 0.85 * edge) * (0.8 + 0.25 * swell + 0.15 * pulseLight)
                + mix(vec3(1.0), base, 0.5) * (spec * 0.5 + fres * 0.25);

            // Glints along the edges, one hashed candidate per half unit of z
            // (index wrapped like the eye, 64 * 2 is whole), twinkling with treble.
            float gc = floor(mod(p.z, WV_ZWRAP) * 2.0);
            float g = hashCell(ivec3(int(gLane) + 2, int(gc), gQ.x > 0.0 ? 1 : 0));
            float tw = 0.5 + 0.5 * sin(uTime * (8.0 + 12.0 * g) + g * 40.0);
            float near = 1.0 - smoothstep(0.0, 0.25, abs(fract(mod(p.z, WV_ZWRAP) * 2.0) - 0.5));
            col += mix(vec3(1.0), base, 0.3) * step(0.86, g) * near * edge * tw * (0.08 + 1.1 * treble);
        }
        col = mix(col, sky, 1.0 - exp(-hitT * fogK));
    }

    // The halo, faded by the fog at the ray's mean depth.
    float gp = glow > 1e-4 ? glowPos / glow : 0.0;
    float sceneT = hitT > 0.0 ? hitT : WV_FAR;
    col += pal(hue + gp) * glow * (0.07 + 0.05 * vol) * (1.0 + k2) * exp(-sceneT * fogK * 0.5);

    col *= 0.8 + 0.25 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
