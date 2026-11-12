#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence bend the trace itself (below), not the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade

// Lissajous: the vectorscope in three dimensions. The flat style drew the
// waveform against itself, x = w(s) and y = w(s + lag). Here the beam gets a
// third axis, z = w(s + 2 lag): a delay embedding, the waveform's phase-space
// portrait, so a pure tone draws a ring, a chord a knotted loop and noise a
// tangled ball. It is drawn as a glowing phosphor tube floating over a dark
// scope graticule (a grid floor with the scope's bright centre cross), with
// the three axes as faint lines through the figure, all in fog.
//
// Underneath the trace sits a closed Lissajous knot,
// (sin(a th + phase), sin(b th), sin(c th + phase)), so silence and quiet
// passages still show a figure. The beam blends from the knot to the
// waveform's portrait with level (and Morph), which is the flat style's
// "waveform drawn against itself" brought in by the music.
//
// ---- camera ------------------------------------------------------------------
//
// One subject, so the eye circles it on the shared rig (camAround) at two
// turns per 1024 world units of travel (an integer multiple of CAM_SPIN_UNIT,
// so the rig's 8192 wrap never shows). Nothing here reads uCamPos or the
// world's z, so no other period has to fit the wrap. The dive is the rig's
// dolly pulling the eye in.
//
// ---- audio -------------------------------------------------------------------
//
// Waveform (sndWave01)          the beam's shape, through the delay embedding.
// Level (SND_LEVEL)             how far the knot gives way to the waveform, and
//                               the palette offset, as in the flat style.
// Middle (SND_MIDDLE)           the halo around the beam (the flat style's glow).
// Kick (SND_KICK_ENV)           the tube thickens: geometry, not brightness.
// Bass (SND_BASS)               the figure swells.
// Hat (SND_HAT_ENV)             the beam's head (the newest samples) flares.
// Mid clock (SND_MID_TIME)      the knot's phase turns, so it tumbles faster on
//                               busy passages and never runs backwards.
// Pitch (SND_HELD_NOTE_CHANGE)  slides the beam's hue.
// Drop (SND_HELD_SURGE)         the figure opens wide, on a hump.
// Hard cut (SND_CUT_COUNT)      picks the knot's frequency ratios.
// SND_ENERGY_TREND              overall level.
//
// Knobs (exactly 0.5, neutral, while Note & key reactions are off):
// k0 PULSE the kick's thickening, k1 TEXTURE the graticule's density,
// k2 FLOW the knot's tumble rate, k3 TONE the figure's build (a high melody
// draws it tall and slim, a low one flat and wide, plus a small hue offset),
// k4 BUILD the halo and how far the knot gives way to the waveform's own
// portrait (tension shows the raw sound), k5 SPACE the orbit radius.
//
// Shared controls: Morph pulls the beam toward the waveform, Warp twists the
// figure about its vertical axis, Turbulence shivers the vertices, Intensity
// and the rest of the grade through lib_scene_grade.
//
// ---- why the distance estimate is safe --------------------------------------
//
// Every deformation (embedding, knot, swell, twist, shiver) moves the
// polyline's VERTICES before the march; space itself is never bent. So the
// field is the exact distance to a union of capsules, Lipschitz 1, and the
// step needs no correction factor.
//
// Per pixel, a segment is kept only if its midpoint lies within its half
// length plus LS_CULL of the view ray's line; every dropped segment is then
// farther than LS_CULL from any point on the ray. The field returns
// min(kept capsules, LS_CULL), which is never more than the true distance, so
// the march cannot overstep a dropped segment. Every vertex stays within 3.26
// of the origin (knot or waveform at most 1.9, shiver 0.28, swell 1.5x; TONE
// only ever shrinks an axis, never stretches one), so
// the tube stays within 3.4 and LS_BOUND adds the cull reach. The nearest the
// rig's dolly brings the eye is 0.65 x 5.4 = 3.51, so the eye is never inside
// the tube, and a ray that misses the bounding sphere never marches at all.

/** Steps per ray: the compile-time ceiling. uSteps is the runtime budget. */
#define LS_MAX_STEPS 80
/** Vertices of the beam (LS_N - 1 capsules). */
#define LS_N 40
#define LS_SEGS 39
/** Window of the waveform the beam spans; s + 2 lag stays inside 0..1, so no wrap jumps. */
#define LS_SPAN 0.6
#define LS_LAG 0.2
/** Any segment farther than this from the ray line is dropped (tube plus halo reach). */
#define LS_CULL 0.42
/** The tube's largest possible radius (3.4) plus LS_CULL. */
#define LS_BOUND 3.9
#define LS_FLOOR -2.4
#define LS_FAR 18.0
#define LS_TAU 6.2831853

/** The kept segments: xyz the start vertex, w its index. */
vec4 gA[LS_SEGS];
vec3 gB[LS_SEGS];
int gCount;
float gRad;
/** Set by map(): the beam parameter 0..1 of the nearest point. */
float gS;

float kn(int i) { return clamp(uKnob[i], 0.0, 1.0) - 0.5; }

vec3 knotRatios() {
    // A count, so it moves only on a hard cut.
    float k = mod(floor(SND_CUT_COUNT + 0.5), 4.0);
    if (k < 0.5) return vec3(1.0, 2.0, 3.0);
    if (k < 1.5) return vec3(3.0, 2.0, 1.0);
    if (k < 2.5) return vec3(2.0, 3.0, 4.0);
    return vec3(3.0, 4.0, 2.0);
}

float capsule(vec3 p, vec3 a, vec3 b, out float h) {
    vec3 pa = p - a;
    vec3 ba = b - a;
    h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

float map(vec3 p) {
    float d = LS_CULL;
    gS = 0.0;
    for (int i = 0; i < LS_SEGS; i++) {
        if (i >= gCount) break;
        float h;
        float di = capsule(p, gA[i].xyz, gB[i], h) - gRad;
        if (di < d) {
            d = di;
            gS = (gA[i].w + h) / float(LS_SEGS);
        }
    }
    return d;
}

vec3 normalAt(vec3 p) {
    vec2 k = vec2(1.0, -1.0);
    float e = 0.002;
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

/** Glow of an axis line through the origin along unit `a`, half length `halfLen`, seen before `tMax`. */
float axisGlow(vec3 ro, vec3 rd, vec3 a, float halfLen, float tMax, float pix) {
    float b = dot(rd, a);
    float denom = 1.0 - b * b;
    if (denom < 1e-4) return 0.0;
    float d0 = dot(rd, ro);
    float e = dot(a, ro);
    float t = (b * e - d0) / denom;
    if (t < 0.0 || t > tMax) return 0.0;
    float s = clamp(e + b * t, -halfLen, halfLen);
    float dist = length(ro + rd * t - a * s);
    float w = max(pix * t, 0.004);
    return w / (dist + w) * 0.5 * (1.0 - smoothstep(halfLen * 0.6, halfLen, abs(e + b * t)));
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camAround(uv, mix(5.4, 7.6, clamp(0.5 + kn(5), 0.0, 1.0)), 2.0 * CAM_SPIN_UNIT, ro, rd);

    float level = clamp(sndShaped(SND_LEVEL), 0.0, 1.5);
    float middle = clamp(sndShaped(SND_MIDDLE), 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float bass = sndLevel(SND_BASS);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float surge = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float hueBase = 0.18 * clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0) + 0.5 * kn(3);

    // ---- the beam: knot blended toward the waveform's portrait ---------------
    float trace = clamp((0.3 + 0.7 * clamp(uMorph, 0.0, 1.0)) * (0.25 + 1.2 * level) + 0.7 * kn(4), 0.0, 1.0);
    // TONE: tall and slim for a high melody, flat and wide for a low one. Each
    // factor is at most 1, so the vertex bound in the header still holds.
    vec3 build = vec3(1.0 - 1.2 * max(kn(3), 0.0), 1.0 + 1.2 * min(kn(3), 0.0), 1.0 - 1.2 * max(kn(3), 0.0));
    vec3 ratio = knotRatios();
    float phase = uTime * 0.15 + SND_MID_TIME * 0.25 * (1.0 + 1.2 * kn(2));
    float scale = 1.15 * (1.0 + 0.1 * bass + 0.15 * surge);
    float twist = 0.9 * clamp(uWarp, 0.0, 2.0);
    float shiver = 0.08 * clamp(uTurbulence, 0.0, 2.0);
    gRad = 0.035 + 0.05 * kick * (1.0 + 2.0 * kn(0));

    // Ray line cull (see the header); one pass builds and compacts the beam.
    gCount = 0;
    vec3 prev = vec3(0.0);
    for (int i = 0; i < LS_N; i++) {
        float fi = float(i);
        float u = fi / float(LS_SEGS);
        float s = u * LS_SPAN;
        vec3 wave = vec3(sndWave01(s), sndWave01(s + LS_LAG), sndWave01(s + 2.0 * LS_LAG)) * 2.0 - 1.0;
        float th = u * LS_TAU;
        vec3 knot = vec3(sin(ratio.x * th + phase), sin(ratio.y * th), sin(ratio.z * th + phase * 0.7)) * 0.8;
        vec3 v = mix(knot, wave * 1.1, trace);
        v += shiver * sin(vec3(fi * 1.7, fi * 2.3, fi * 2.9) + uTime * vec3(3.1, 2.7, 3.7));
        v *= scale * build;
        float ta = twist * v.y;
        v.xz = mat2(cos(ta), -sin(ta), sin(ta), cos(ta)) * v.xz;
        if (i > 0) {
            vec3 m = 0.5 * (prev + v);
            float hl = 0.5 * length(v - prev);
            vec3 q = m - ro;
            float tm = dot(q, rd);
            if (length(q - rd * tm) < hl + LS_CULL && tm > -hl - LS_CULL) {
                gA[gCount] = vec4(prev, fi - 1.0);
                gB[gCount] = v;
                gCount++;
            }
        }
        prev = v;
    }

    // ---- march, only inside the bounding sphere ------------------------------
    float bt = dot(-ro, rd);
    float bh = bt * bt - (dot(ro, ro) - LS_BOUND * LS_BOUND);
    float hitT = -1.0;
    float md = LS_CULL;
    float mdS = 0.0;
    float haze = 0.0;
    if (bh > 0.0 && gCount > 0) {
        float sq = sqrt(bh);
        float t = max(bt - sq, 0.0);
        float tEnd = bt + sq;
        for (int i = 0; i < LS_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            float d = map(ro + rd * t);
            if (d < md) {
                md = d;
                mdS = gS;
            }
            haze += exp(-max(d, 0.0) * 9.0) * min(max(d, 0.02), 0.25);
            if (d < 0.001) {
                hitT = t;
                break;
            }
            t += d * 0.9;
            if (t > tEnd) break;
        }
    }

    // ---- background: dark scope glass, the graticule floor, the axes ---------
    vec3 col = pal(hueBase + 0.6) * 0.025 * (1.0 + 0.6 * clamp(rd.y + 0.3, 0.0, 1.0));
    float pix = 2.0 / (uResolution.y * max(uCamFocal, 0.2));
    float surfT = hitT > 0.0 ? hitT : LS_FAR;
    if (rd.y < -1e-3) {
        float tf = (LS_FLOOR - ro.y) / rd.y;
        if (tf > 0.0 && tf < surfT) {
            vec2 fp = (ro + rd * tf).xz;
            float dens = mix(1.4, 3.2, clamp(0.5 + kn(1), 0.0, 1.0));
            vec2 gq = abs(fract(fp * dens - 0.5) - 0.5) / dens;
            float w = pix * tf * 1.2;
            float lines = max(w / (gq.x + w), w / (gq.y + w)) * 0.35;
            // The scope's centre cross, brighter.
            float cross2 = max(w / (abs(fp.x) + w), w / (abs(fp.y) + w));
            float fade = exp(-tf * 0.12) * (1.0 - smoothstep(4.5, 9.0, length(fp)));
            vec3 floorCol = pal(hueBase + 0.45) * (lines * 0.35 + cross2 * 0.5) * fade;
            // The beam's light pooling under it.
            floorCol += pal(hueBase + 0.1 + level) * exp(-length(fp) * 0.9) * (0.06 + 0.12 * level) * fade;
            col += floorCol;
            surfT = min(surfT, tf);
        }
    }
    float ax = axisGlow(ro, rd, vec3(1.0, 0.0, 0.0), 2.6, surfT, pix)
             + axisGlow(ro, rd, vec3(0.0, 1.0, 0.0), 2.0, surfT, pix)
             + axisGlow(ro, rd, vec3(0.0, 0.0, 1.0), 2.6, surfT, pix);
    col += vec3(0.6, 0.7, 0.8) * ax * 0.18;

    // ---- the beam -----------------------------------------------------------
    // The newest samples (s near 1) burn brightest, like a phosphor's head.
    float headAt = mdS;
    float hueAt = hueBase + md * 2.0 + level * 0.5 + 0.35 * headAt;
    float halo = (0.5 + 0.8 * middle) * (1.0 + 0.8 * kn(4));
    float persist = 0.45 + 0.55 * headAt + 0.6 * hat * smoothstep(0.7, 1.0, headAt);
    col += pal(hueAt) * (exp(-max(md, 0.0) * 18.0) * 0.35 + haze * 0.5) * halo * persist;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p);
        map(p);
        float sAt = gS;
        float facing = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - facing, 3.0);
        float hue = hueBase + level * 0.5 + 0.35 * sAt;
        float pers = 0.45 + 0.55 * sAt + 0.6 * hat * smoothstep(0.7, 1.0, sAt);
        vec3 L = normalize(vec3(-0.4, 0.8, 0.45));
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 40.0);
        // A hot core facing the eye, coloured at the rim: an emissive tube.
        vec3 tube = mix(pal(hue), vec3(1.0), 0.55 * pow(facing, 4.0)) * (0.35 + 0.75 * pow(facing, 2.0))
                  + pal(hue + 0.1) * fres * 0.6 + vec3(spec) * 0.4;
        col = tube * pers + col * 0.2;
        col = mix(col, pal(hueBase + 0.6) * 0.03, 1.0 - exp(-hitT * 0.03));
    }

    col *= 0.8 + 0.3 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.6, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
