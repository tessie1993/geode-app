#version 300 es
precision highp float;

// ACID - the feedback pass.
//
// A video-synthesis loop in the classic public algebra: the previous frame is
// re-sampled through a style-owned coordinate warp (zoom, rotation, polar
// folds, log-polar scroll, block glitch, mirror, smear), colour-rotated,
// attenuated below unity, and a live audio-drawn source layer is added into
// the loop. Ten substyles are ten different warp/colour/source recipes, and
// each one jumps to a second recipe of its own on the music's hard cuts.
//
// WHO MOVES WHAT. The scene (AcidScene.cpp) conducts the loop from the sound
// engine's whole frame, not from three bands and a beat: every uniform below
// that moves is already a musical decision made on the CPU (the kick punches
// the zoom, the snare kicks the rotation and flips the mirror, the hat
// shimmers, a random-per-kick re-aims the vanishing point, the melody lifts
// it and steps the hue, sections switch the recipe). This pass only applies
// them, so the GPU work per pixel stays a fixed handful of texture reads.
//
// The audio layer can also MODULATE the warp - its brightness displaces where
// the feedback re-samples - which is what welds the trails to the music
// instead of layering a picture on top of an unrelated echo.
//
// SAFETY / STABILITY: uFeedback is clamped below 1 in the scene, every sample
// is clamped on read (a NaN in a feedback loop is forever), injection is
// bounded, and hue rotation is a rate, not a strobe. The global composite and
// flash budget run downstream like every scene.

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uPrev;
uniform vec2 uRes;
uniform int uStyle;       // warp recipe, 0..9 (the style's own, or its hard-cut alternate)
uniform int uSource;      // audio source layer recipe, 0..3
uniform float uZoom;      // per-frame feedback zoom (1 = none), already punched by kick/surge
uniform float uRotate;    // per-frame feedback rotation, radians (never reverses: clock + snare)
uniform float uHueShift;  // per-frame hue rotation, turns
uniform float uFeedback;  // survival gain, < 1 (echo length follows the energy trend)
uniform float uModulate;  // how far source brightness displaces the resample
uniform float uGlitch;    // beat-gated glitch amount, 0..1
uniform float uEpoch;     // integer re-roll for glitch block offsets
uniform float uClock;     // the sound clock: seconds that run with the music and never backwards
uniform float uBass;
uniform float uMid;
uniform float uTreble;
uniform float uBeat;
uniform float uStrike;
uniform float uDrive;
// Twelve live spectral spokes, 0..1: the current band envelopes folded into a wheel.
uniform float uSpokes[12];
uniform float uBaseHue;
uniform float uHueSpan;
uniform float uLiquid;    // sine-field liquid warp amount (melody wobble already folded in)

// ---- the conductor's per-frame moves (AcidScene::conduct) -------------------
/** The vanishing point, q units: x re-aimed per kick, y lifted by the melody's pitch height. */
uniform vec2 uCentre;
/** Local vortex around the centre, radians per frame; its sign flips with the kick toggle. */
uniform float uTwist;
/** Hat shimmer: fine high-frequency resample jitter, q units. */
uniform float uShimmer;
/** Stereo drift: per-frame resample offset, q units (the echo slides toward the sound). */
uniform vec2 uDrift;
/** Mirror fold 0..1 (the snare's flash, or the style's own), and which half survives (+-1, snare toggle). */
uniform float uMirror;
uniform float uMirrorSide;
/** Melody wobble 0..1 (glide + vibrato): bends the liquid field's frequency. */
uniform float uWobble;
/** Drum envelopes 0..1 for the drum source layer. */
uniform float uKickEnv;
uniform float uSnareEnv;
uniform float uHatEnv;
/** Onset count: re-rolls the snare's star and the hat's sparks per hit. */
uniform float uSeed;
/** The lead note: x = pitch class (turns), y = pitch height 0..1, z = how strongly to draw it, w = hue. */
uniform vec4 uMelody;

// ---- the finger as a source ------------------------------------------------
//
// A finger draws into the SOURCE layer, upstream of everything: the loop then
// zooms it, folds it, rotates its hue and smears it exactly as it does the
// audio-drawn figure, so a swipe becomes a trail that keeps travelling for as
// long as the feedback holds it. It also lands before srcLuma is measured, so
// the finger displaces where the echo re-samples too - the picture bends
// toward it, not just brightens under it. See SceneTouch.kt for the packing
// (xy is y-up NDC, aspect NOT applied).
#define TOUCH_MAX_POINTS 5
/** Per finger: xy = position, z = strength 0..1, w = age in seconds. */
uniform vec4 uTouchPoints[TOUCH_MAX_POINTS];
/** Occupied slots, including ones still fading after release. 0 = nothing touched. */
uniform int uTouchCount;

const float TAU = 6.2831853;

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

vec2 hash2(vec2 q) {
    q = vec2(dot(q, vec2(127.1, 311.7)), dot(q, vec2(269.5, 183.3)));
    return fract(sin(q) * 43758.5453);
}

// Approximate linear hue rotation; cheap and loop-stable (no rgb<->hsv per
// frame, which quantizes badly in an 8-bit feedback).
vec3 hueRotate(vec3 c, float turns) {
    float a = turns * TAU;
    const vec3 W = vec3(0.299, 0.587, 0.114);
    float luma = dot(c, W);
    vec3 chroma = c - luma;
    // Rotate chroma around the grey axis (Rodrigues, axis = (1,1,1)/sqrt3).
    const vec3 K = vec3(0.57735);
    vec3 rotated = chroma * cos(a) + cross(K, chroma) * sin(a) + K * dot(K, chroma) * (1.0 - cos(a));
    return luma + rotated;
}

mat2 rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

// -- the audio-drawn source layer -------------------------------------------

// 0: spectral mandala - twelve spokes, one per spectrum slice.
// 1: band rings - three breathing rings, bass inside, treble outside.
// 2: radial bars - a coarse circular spectrum from the three envelopes.
// 3: orbit ribbon - a Lissajous-like orbit whose radius rides the strike.
vec3 sourceLayer(vec2 q, float r, float ang) {
    if (uSource == 0) {
        vec3 acc = vec3(0.0);
        for (int i = 0; i < 12; i++) {
            float a = TAU * (float(i) / 12.0);
            float d = abs(mod(ang - a + TAU * 1.5, TAU) - TAU * 0.5);
            float spoke = exp(-d * d * 90.0) * uSpokes[i];
            float radial = exp(-pow((r - 0.32 - 0.25 * uSpokes[i]) * 6.0, 2.0));
            acc += hsv2rgb(vec3(fract(uBaseHue + uHueSpan * float(i) / 12.0), 0.8, 1.0)) * spoke * radial;
        }
        return acc * (0.4 + 0.6 * uBass);
    }
    if (uSource == 1) {
        float rings = 0.0;
        rings += exp(-pow((r - 0.18 - 0.10 * uBass) * 22.0, 2.0)) * uBass;
        rings += exp(-pow((r - 0.42 - 0.08 * uMid) * 26.0, 2.0)) * uMid * 0.8;
        rings += exp(-pow((r - 0.66 - 0.06 * uTreble) * 30.0, 2.0)) * (uTreble + uStrike * 0.5) * 0.65;
        return hsv2rgb(vec3(fract(uBaseHue + uHueSpan * r), 0.75, 1.0)) * rings;
    }
    if (uSource == 2) {
        float seg = floor(mod(ang / TAU + 1.0, 1.0) * 24.0);
        float h = fract(sin(seg * 12.99) * 437.585);
        float level = mix(uBass, mix(uMid, uTreble, step(0.66, h)), step(0.33, h));
        float bar = step(r, 0.15 + 0.55 * level) * step(0.12, r);
        float edge = smoothstep(0.5, 0.0, abs(fract(mod(ang / TAU + 1.0, 1.0) * 24.0) - 0.5));
        return hsv2rgb(vec3(fract(uBaseHue + uHueSpan * h * 0.4), 0.8, 1.0)) * bar * edge * 0.8;
    }
    float ph = uClock * 0.9;
    vec2 orbit = 0.45 * vec2(sin(ph * 3.0 + uBass * 2.0), sin(ph * 4.0 + 1.3));
    float d = length(q - orbit);
    float dot1 = exp(-d * d * 260.0) * (0.5 + uStrike + uBeat);
    return hsv2rgb(vec3(fract(uBaseHue + uTreble * 0.3), 0.7, 1.0)) * dot1 * 1.6;
}

/**
 * The drums and the melody drawn straight into the loop, on top of the style's
 * recipe, so the echo is FULL of the song's events and not only its level:
 *
 *  - kick: a ring born at the vanishing point that the envelope's decay
 *    throws outward; the zoom then carries it on down the tunnel as a stack
 *    of rings, one per kick, which is what makes the depth read.
 *  - snare: an eight-point star whose angle re-rolls per onset.
 *  - hat: a sparse spray of single-texel sparks, a different set per hit.
 *  - melody: a dot at the note's pitch class on a wheel whose radius is the
 *    pitch height, so a line sung or played draws its own contour, and the
 *    feedback spins it into a ribbon.
 *
 * No loops: four closed forms, bounded, fixed cost.
 */
vec3 eventLayer(vec2 q) {
    vec3 acc = vec3(0.0);
    vec2 c = q - uCentre;
    float rc = length(c);
    if (uKickEnv > 0.01) {
        float ringR = 0.06 + 0.5 * (1.0 - uKickEnv);
        float ring = exp(-pow((rc - ringR) * 34.0, 2.0)) * uKickEnv;
        acc += hsv2rgb(vec3(fract(uBaseHue + 0.5 * uHueSpan), 0.65, 1.0)) * ring * 0.55;
    }
    if (uSnareEnv > 0.01) {
        float a0 = fract(sin(uSeed * 7.13) * 917.3) * TAU;
        float a = atan(c.y, c.x) - a0;
        float seg = TAU / 8.0;
        float d = abs(mod(a + seg * 0.5, seg) - seg * 0.5);
        float star = exp(-d * d * 260.0) * exp(-rc * 3.5) * uSnareEnv;
        acc += hsv2rgb(vec3(fract(uBaseHue + 0.15 + 0.3 * uHueSpan), 0.35, 1.0)) * star * 0.6;
    }
    if (uHatEnv > 0.02) {
        vec2 cell = floor(q * 48.0);
        vec2 h = hash2(cell + vec2(uSeed * 1.7, uSeed * 0.3));
        vec2 f = fract(q * 48.0) - 0.5;
        float spark = step(h.x, 0.05 * uHatEnv) * exp(-dot(f, f) * 30.0);
        acc += vec3(spark * uHatEnv * 0.7);
    }
    if (uMelody.z > 0.01) {
        float a = uMelody.x * TAU + uClock * 0.2;
        vec2 p = (0.12 + 0.36 * uMelody.y) * vec2(cos(a), sin(a));
        vec2 d = q - p;
        float dot1 = exp(-dot(d, d) * 900.0) * uMelody.z;
        acc += hsv2rgb(vec3(fract(uMelody.w), 0.6, 1.0)) * dot1;
    }
    return acc;
}

/** Radius of a finger's spark, in the same units as q (half-screen = 0.5). */
#define TOUCH_SPARK_RADIUS 0.06
/** Ceiling on the summed spark, per channel. */
#define TOUCH_SPARK_CAP 0.9

/**
 * The fingers, drawn as palette sparks.
 *
 * Hue walks with the finger's AGE rather than with time, so a held finger
 * paints a gradient along its own path while a tap stays one colour - the
 * gesture writes its own duration into the picture. 0.12 turns a second: a
 * long drag crosses a third of the wheel, which reads as one continuous
 * ribbon shading rather than as a rainbow.
 *
 * Capped, not normalized, for the same reason the pedestal below exists: this
 * is an 8-bit loop with a gain just under 1, so anything injected at full
 * white sits in the feedback for seconds. 0.9 leaves the top of the range to
 * the crossings and the overdrive.
 */
vec3 touchSpark(vec2 q, float aspect) {
    vec3 acc = vec3(0.0);
    for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
        if (i >= uTouchCount) break;
        vec4 t = uTouchPoints[i];
        if (t.z <= 0.0) continue;
        vec2 d = q - vec2(t.x * 0.5 * aspect, t.y * 0.5);
        float g = t.z * exp(-dot(d, d) / (TOUCH_SPARK_RADIUS * TOUCH_SPARK_RADIUS));
        acc += hsv2rgb(vec3(fract(uBaseHue + uHueSpan * 0.12 * t.w), 0.7, 1.0)) * g;
    }
    return min(acc, vec3(TOUCH_SPARK_CAP));
}

// -- the style warps ---------------------------------------------------------

// Every warp runs about the vanishing point (uCentre), so re-aiming it on a
// kick swings the whole tunnel - folds, spiral throat and zoom together.
vec2 warp(vec2 q) {
    q -= uCentre;
    float r = length(q);
    float ang = atan(q.y, q.x);
    if (uStyle == 2) {
        // six-fold polar wedge fold
        float seg = TAU / 6.0;
        float a = abs(mod(ang + uClock * 0.05, seg) - seg * 0.5);
        q = r * vec2(cos(a), sin(a));
    } else if (uStyle == 3) {
        // log-polar scroll: endless spiral throat
        float lr = log(max(r, 1e-3));
        lr -= 0.012 + 0.02 * uBass;
        ang += 0.35 * lr * 0.15 + 0.006;
        float er = exp(lr);
        q = er * vec2(cos(ang), sin(ang));
    } else if (uStyle == 5 && uGlitch > 0.001) {
        // block displacement: quantized cells jump while the glitch is hot
        float cells = 14.0;
        vec2 id = floor((q * 0.5 + 0.5) * cells);
        vec2 h = hash2(id + uEpoch);
        if (h.x < uGlitch * 0.6) q += (h - 0.5) * 0.22 * uGlitch;
    } else if (uStyle == 6) {
        // scanline shear + slow vertical roll
        float row = floor((q.y * 0.5 + 0.5) * uRes.y / 3.0);
        float h = hash2(vec2(row, uEpoch)).x;
        q.x += (h - 0.5) * 0.05 * uGlitch;
        q.y += 0.0035;
    } else if (uStyle == 8) {
        // four-fold mirror room: fold both axes, then slide into the corner
        // so the reflections crawl instead of freezing
        q = abs(q) - 0.02;
    } else if (uStyle == 9) {
        // directional smear along a slowly turning angle
        float a = uClock * 0.11;
        q += vec2(cos(a), sin(a)) * (0.006 + 0.01 * uMid);
    }
    // The classic liquid field: each axis's sample point sways on a sine of
    // the OTHER axis, drifting with the sound clock. The melody's glide and
    // vibrato bend its frequency, so a sliding note melts the picture in a
    // different grain than a held one.
    if (uLiquid > 0.0) {
        float amp = uLiquid * (0.010 + 0.010 * uMid);
        float k = 7.0 + 5.0 * uWobble;
        q += vec2(
            sin(q.y * k + uClock * 1.3),
            cos(q.x * k + uClock * 1.1)
        ) * amp;
    }
    // Local vortex: strongest at the vanishing point, gone by the edges, so
    // the kick toggle visibly reverses a whirlpool while the frame's spin
    // keeps its direction.
    if (abs(uTwist) > 1e-5) {
        q = rot(uTwist * exp(-dot(q, q) * 5.0)) * q;
    }
    // Hat shimmer: a fine, fast ripple of the resample point.
    if (uShimmer > 1e-5) {
        q += uShimmer * vec2(sin(q.y * 85.0 + uClock * 31.0), cos(q.x * 85.0 + uClock * 29.0));
    }
    // zoom + rotation, shared by every style (their rates differ per style)
    q = rot(uRotate) * q;
    return q / uZoom + uCentre + uDrift;
}

/** The style's resample of the previous frame at uv (its sampling recipe, not its warp). */
vec3 samplePrev(vec2 backUv) {
    if (uStyle == 4) {
        // prism drift: each channel re-samples at its own zoom, so the echo
        // fringes into a spectrum as it recedes
        vec2 c = backUv - 0.5;
        return vec3(
            texture(uPrev, c * 0.9985 + 0.5).r,
            texture(uPrev, c + 0.5).g,
            texture(uPrev, c * 1.0015 + 0.5).b
        );
    }
    if (uStyle == 1) {
        // Infinite-mirror well from one frame: the echo re-samples itself at
        // three concentric scales, so depth appears without a history ring.
        vec2 c = backUv - 0.5;
        return 0.45 * texture(uPrev, backUv).rgb +
            0.30 * texture(uPrev, c * 0.55 + 0.5).rgb +
            0.25 * texture(uPrev, c * 0.30 + 0.5).rgb;
    }
    return texture(uPrev, backUv).rgb;
}

void main() {
    float aspect = uRes.x / uRes.y;
    vec2 q = (vUv - 0.5) * vec2(aspect, 1.0);
    float r = length(q);
    float ang = atan(q.y, q.x);

    vec3 src = (sourceLayer(q, r, ang) + eventLayer(q)) * uDrive;
    // Outside uDrive on purpose: a finger is not audio, and it has to answer
    // even with Audio drive at zero.
    if (uTouchCount > 0) src += touchSpark(q, aspect);

    // hydra-style modulation: source brightness displaces the resample point.
    float srcLuma = dot(src, vec3(0.3333));
    vec2 w = warp(q) + srcLuma * uModulate * normalize(q - uCentre + vec2(1e-4)) * -0.02;

    vec3 prev = samplePrev(w / vec2(aspect, 1.0) + 0.5);
    // Mirror fold: blend toward the frame folded about the vanishing point's
    // vertical, keeping the half the snare toggle chose. A blend, not a cut,
    // so the snare's flash ghosts a symmetric twin into the echo and lets it
    // go again instead of snapping the picture.
    if (uMirror > 0.01) {
        vec2 m = vec2(uCentre.x + uMirrorSide * abs(w.x - uCentre.x), w.y);
        prev = mix(prev, samplePrev(m / vec2(aspect, 1.0) + 0.5), uMirror);
    }
    prev = clamp(prev, vec3(0.0), vec3(1.0));
    // Black pedestal: the loop lives in 8 bits, and per-frame hue rotation
    // walks quantization error into full-field grey shimmer if the smallest
    // values are allowed to persist. Crushing them costs nothing visible -
    // real content is orders of magnitude above it - and the shimmer dies.
    prev = max(prev - vec3(0.0045), vec3(0.0));
    prev = hueRotate(prev, uHueShift);

    // Solar substyle: fold the feedback around mid-grey before it decays -
    // the classic solarized-video look.
    if (uStyle == 7) prev = abs(prev - 0.5) * 1.9;

    vec3 color = prev * uFeedback + min(src, vec3(1.2));
    fragColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
