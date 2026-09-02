#version 300 es
precision highp float;
// GLSL ES 3.00 defaults sampler2D to LOWP (range [-2,2), ~8 fraction
// bits) in the vertex stage too. uWave is R32F waveform data; on GPUs
// honoring sampler precision (Mali) every texelFetch is clamped and
// quantized.
precision highp sampler2D;

// Oscilloscope beam geometry: one quad per waveform segment, expanded here so
// the fragment stage can integrate a Gaussian beam along it.
//
// Ported from woscope (MIT, Igor Null and Chad von Nau) - see
// THIRD_PARTY_NOTICES. woscope draws from a vertex buffer of sample pairs; this
// reads the waveform from a texture and derives everything from gl_VertexID, so
// a frame uploads one 1D texture instead of rebuilding a VBO.

/** Waveform samples in the red channel, one per texel, -1..1. */
uniform sampler2D uWave;
/** Number of segments drawn, i.e. samples - 1 (sweep) or samples (XY). */
uniform int uCount;
/** 0 = time sweep (x is time), 1 = XY (two channels against each other). */
uniform float uMode;
/** Beam half-width in normalized units - the sigma the fragment integrates. */
uniform float uSigma;
/** Output aspect, so the beam stays round on a non-square screen. */
uniform float uAspect;
/** Vertical gain of the trace. */
uniform float uGain;
/** Quarter-cycle offset used to fake the second channel; see CymaticsScene. */
uniform int uPhaseOffset;

// ---- the finger as a deflection plate --------------------------------------
//
// A real scope's beam is steered by a field, not by redrawing the signal, and
// that is exactly what a finger does here: the trace is pulled toward it, most
// where it passes closest, and the segments that get compressed by the pull
// brighten on their own because the fragment stage divides beam energy by the
// length actually travelled. Nothing about the waveform changes - it is still
// the same samples, deflected on the way to the screen. See SceneTouch.kt for
// the packing (xy is y-up NDC, aspect NOT applied).
#define TOUCH_MAX_POINTS 5
/** Per finger: xy = position, z = strength 0..1, w = age in seconds. */
uniform vec4 uTouchPoints[TOUCH_MAX_POINTS];
/** Occupied slots, including ones still fading after release. 0 = nothing touched. */
uniform int uTouchCount;

/**
 * Peak fraction of the offset a finger pulls out of the trace.
 *
 * Held below 1 because the deflection is a map s -> s - d*g(d), whose radial
 * factor 1 - g stays positive only while g does not reach 1. At 1 the trace
 * folds through the finger and the beam crosses itself; at 0.5 the pull is
 * plainly visible and every segment keeps its order along the sweep.
 */
#define TOUCH_PULL 0.5
/** Radius of the pull, in aspect-corrected screen units (half-screen = 1). */
#define TOUCH_PULL_RADIUS 0.45

/** Bend one point of the trace toward the fingers. Identity when untouched. */
vec2 deflect(vec2 s) {
    vec2 pull = vec2(0.0);
    float gain = 0.0;
    for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
        if (i >= uTouchCount) break;
        vec4 t = uTouchPoints[i];
        if (t.z <= 0.0) continue;
        vec2 d = s - vec2(t.x * uAspect, t.y);
        float g = t.z * TOUCH_PULL * exp(-dot(d, d) / (TOUCH_PULL_RADIUS * TOUCH_PULL_RADIUS));
        pull += d * g;
        gain += g;
    }
    // Several fingers in one place would sum past 1 and turn the pull inside
    // out - the trace would be pushed through and out the far side. Rescaling
    // the whole sum back to one finger's worth keeps the map monotone and
    // makes a pinch read as one firm pull, which is what it feels like.
    return s - pull * (gain > TOUCH_PULL ? TOUCH_PULL / gain : 1.0);
}

/** Position relative to the segment: x along it, y across it. */
out vec2 vLocal;
/** Length of this segment in the same units, for the dwell normalization. */
out float vLen;
/** 0..1 along the trace, so the fragment can fade the tail. */
out float vAge;

vec2 samplePoint(int index) {
    int n = max(uCount, 1);
    float w = texelFetch(uWave, ivec2(index % textureSize(uWave, 0).x, 0), 0).r;
    if (uMode < 0.5) {
        // Time sweep: x walks the screen, y is the sample.
        return vec2(float(index) / float(n) * 2.0 - 1.0, w * uGain);
    }
    // XY: this sample against one a quarter cycle away.
    int other = (index + uPhaseOffset) % textureSize(uWave, 0).x;
    float w2 = texelFetch(uWave, ivec2(other, 0), 0).r;
    return vec2(w * uGain, w2 * uGain);
}

void main() {
    int seg = gl_VertexID / 6;
    int corner = gl_VertexID % 6;
    vec2 p0 = samplePoint(seg);
    vec2 p1 = samplePoint(seg + 1);

    // Aspect-correct BEFORE measuring the segment: a beam that is round on
    // screen has to be round in the space the integral is computed in. The
    // deflection lands in the same space and before the segment is measured,
    // so a pull that bunches the trace shortens vLen and the dwell term
    // brightens it, exactly as a slowed beam brightens on a real tube.
    vec2 a = vec2(p0.x * uAspect, p0.y);
    vec2 b = vec2(p1.x * uAspect, p1.y);
    if (uTouchCount > 0) {
        a = deflect(a);
        b = deflect(b);
    }
    vec2 dir = b - a;
    float len = length(dir);
    vec2 tangent = len > 1e-6 ? dir / len : vec2(1.0, 0.0);
    vec2 normal = vec2(-tangent.y, tangent.x);

    // Quad padded by 3 sigma each way: past that the Gaussian is under a
    // thousandth and the extra fill rate buys nothing.
    float pad = uSigma * 3.0;
    // Two triangles, corners in order (-,-) (+,-) (-,+) / (+,-) (+,+) (-,+).
    float along = (corner == 1 || corner == 3 || corner == 4) ? len + pad : -pad;
    float across = (corner == 2 || corner == 4 || corner == 5) ? pad : -pad;

    vec2 pos = a + tangent * along + normal * across;
    vLocal = vec2(along, across);
    vLen = len;
    vAge = float(seg) / float(max(uCount, 1));
    gl_Position = vec4(pos.x / uAspect, pos.y, 0.0, 1.0);
}
