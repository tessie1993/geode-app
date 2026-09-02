// SILK - the field-advection step.
//
// A SimSpec STEP BODY, not a whole shader. It defines simStep() and nothing
// around it: no #version, no precision directives, no in/out, no main().
// SimGlsl.kt supplies all four, and supplies them DIFFERENTLY per path - `300
// es` with a colour attachment on the ES 3.0 fragment ping-pong, `310 es` with
// an imageStore on the ES 3.1 compute dispatch. A body that carried its own
// header could only ever be right for one of them, which is why it carries
// none. See SimSpec's KDoc for the full contract.
//
// Everything this body knows about its state, it knows through three generated
// helpers: simLoad(ivec2) for a whole texel, simSample(vec2) for a continuous
// coordinate, simUv(ivec2) for a texel centre. It cannot tell whether those
// four floats are halves or pre-scaled bytes, and it cannot tell whether it is
// running as a dispatch or as a fragment. That is the entire reason there is
// one file here rather than two.
//
// The whole family is this one texture: three "band lanes" of luminous dye
// (r = bass, g = mid, b = treble deposits) advected through a smooth 3D
// velocity field and re-fed every frame. No particles exist anywhere - the
// filaments are the dye's own history, stretched along the flow, which is why
// the picture reads as silk rather than as sprites.
//
// The field library is a cyclic-sine family around the published Thomas
// attractor form sin(p.yzx) - b*p, plus two intrinsically 2D fields (a curl
// of simplex noise, and a magnetic-pendulum pole field). uField selects one;
// styles never blend fields mid-frame - identity comes from the field, and
// the damping parameter b breathes slowly on the scene clock instead.
//
// SAFETY / STABILITY: velocities are clamped, the feedback survives only
// through uDecay < 1, and every sample is sanitized - a NaN entering the
// ping-pong would otherwise persist forever.

uniform int uField;        // 0..9, see fieldAt()
uniform float uB;          // damping/contraction parameter, breathes slowly
uniform float uAdvect;     // dt * flow, in field units per frame
uniform float uDecay;      // feedback survival per frame, < 1
uniform float uFieldScale; // field domain zoom
uniform float uSwirl;      // how much field z feeds screen rotation
uniform float uSlabX;      // cos/sin pair of the slab orbit phase
uniform float uSlabY;
uniform float uSeedEpoch;  // integer epoch; the stroke lattice re-seats when it steps
uniform float uDrift;      // continuous slow time for the curl field's evolution
uniform float uStrokes;    // injection density multiplier
uniform float uElong;      // filament elongation along the flow
uniform float uDrive;      // audio drive onto injection brightness
uniform float uBass;       // slewed band envelopes, 0..~1.2
uniform float uMid;
uniform float uTreble;
uniform float uBeat;       // graded beat envelope, 0..1.5
uniform float uStrike;     // raw-PCM transient, 0..1.5
uniform float uBeatRing;   // expanding ring radius since the last beat, <0 = none

// There is no uRes and no uStateScale here any more. The grid size arrives as
// simStep's `size` argument, and the dye's range is the layer's business: it
// folds the RGBA8 fallback's scale into simLoad/simSample on the way in and out
// again on the way to storage, as a compile-time constant, so a body that
// multiplied by it would be applying it twice.

// ---- the finger as a source ------------------------------------------------
//
// Everything above injects where the STROKE LATTICE says to. A finger is the
// one seed the user places: dye is laid down under it in all three band lanes
// and the flow then does what it does to any other dye - stretches it into
// filaments and carries it away. Nothing else changes, which is the point;
// touch is a source term, not a second engine. See SceneTouch.kt for the
// packing (xy is y-up NDC, aspect NOT applied).
#define TOUCH_MAX_POINTS 5
/** Per finger: xy = position, z = strength 0..1, w = age in seconds. */
uniform vec4 uTouchPoints[TOUCH_MAX_POINTS];
/** Occupied slots, including ones still fading after release. 0 = nothing touched. */
uniform int uTouchCount;

const float TAU = 6.2831853;

// -- field library -----------------------------------------------------------

vec3 fieldAt(int t, vec3 p, float b) {
    if (t == 0) return sin(p.yzx) - b * p;
    if (t == 1) return cos(p.yzx) - b * p;
    if (t == 2) return sin(p.yzx) * sin(p.zxy) - b * p;
    if (t == 3) return sin(p.yzx) * cos(2.2 * length(p)) - b * p;
    if (t == 4) return sin(p.yzx + 0.45 * p.zxy * p.zxy) - b * p;
    if (t == 5) return sin(p.yzx + sin(p.zxy + sin(p.xyz))) - b * p;
    if (t == 6) return tanh(2.2 * p.yzx) - b * p;
    return sin(p.yzx + 0.75 * sin(p.zxy)) - b * p; // 7
}

// Simplex-ish value noise, cheap and periodic enough for a curl field.
vec2 hash2(vec2 q) {
    q = vec2(dot(q, vec2(127.1, 311.7)), dot(q, vec2(269.5, 183.3)));
    return fract(sin(q) * 43758.5453);
}

float vnoise(vec2 q) {
    vec2 i = floor(q);
    vec2 f = fract(q);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash2(i).x;
    float b = hash2(i + vec2(1.0, 0.0)).x;
    float c = hash2(i + vec2(0.0, 1.0)).x;
    float d = hash2(i + vec2(1.0, 1.0)).x;
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
}

// Curl of a scalar noise potential: divergence-free swirls.
vec2 curlField(vec2 q, float epoch) {
    float e = 0.15;
    vec2 dq = vec2(
        vnoise(q + vec2(0.0, e) + epoch) - vnoise(q - vec2(0.0, e) + epoch),
        vnoise(q + vec2(e, 0.0) + epoch) - vnoise(q - vec2(e, 0.0) + epoch)
    );
    return vec2(dq.x, -dq.y) / (2.0 * e);
}

// Three attracting poles with a tangential component: pendulum-like orbits.
vec2 poleField(vec2 q, float b) {
    vec2 v = vec2(0.0);
    for (int i = 0; i < 3; i++) {
        float a = TAU * (float(i) / 3.0 + 0.083);
        vec2 pole = 1.05 * vec2(cos(a), sin(a));
        vec2 d = pole - q;
        float r2 = dot(d, d) + 0.09;
        v += d / r2 * 0.6 + vec2(-d.y, d.x) / r2 * 0.45;
    }
    return v - b * 3.0 * q;
}

// -- injection ---------------------------------------------------------------

// Field-aligned luminous strokes at jittered grid seeds. Each seed belongs to
// one band lane (bass wide and central, treble fine and outer), so the music
// literally paints in its own colours.
//
// A GATHER, not a scatter: every texel evaluates the lattice cells around it
// and sums their contribution, so this ports to a dispatch unchanged. That is
// also the honest limit of what compute buys Silk - the removed pass overhead
// and nothing more. The families whose injection is a scatter (Myco's agents,
// the fluid's splats) are the ones that collect the real tiler argument, and
// none of them is this one.
vec3 strokes(vec2 q, vec2 dir) {
    vec3 acc = vec3(0.0);
    float grid = 5.0 * uStrokes;
    vec2 cell = floor(q * grid + uSeedEpoch);
    // 3x3 neighbourhood so strokes cross cell borders without popping.
    for (int oy = -1; oy <= 1; oy++) {
        for (int ox = -1; ox <= 1; ox++) {
            vec2 id = cell + vec2(float(ox), float(oy));
            vec2 h = hash2(id + uSeedEpoch * 0.37);
            vec2 seed = (id + h) / grid;
            vec2 d = q - seed;
            // lane: 0 bass / 1 mid / 2 treble by seed hash
            float lanePick = fract(h.x * 7.31 + h.y * 3.17);
            float along = dot(d, dir);
            float across = dot(d, vec2(-dir.y, dir.x));
            float elong = uElong * (1.0 + 2.0 * lanePick);
            float g = exp(-(along * along) / (0.004 * elong) - (across * across) / 0.00025);
            float w = smoothstep(0.15, 0.9, h.y);
            if (lanePick < 0.34) {
                acc.r += g * w * uBass;
            } else if (lanePick < 0.67) {
                acc.g += g * w * uMid;
            } else {
                acc.b += g * w * (uTreble + 0.5 * uStrike);
            }
        }
    }
    return acc;
}

/** Radius of a finger's deposit, as a fraction of the half-screen. */
#define TOUCH_INK_RADIUS 0.09
/** Ceiling on the summed deposit, per lane. */
#define TOUCH_INK_CAP 1.5

/**
 * Dye laid down under the fingers, in field units.
 *
 * The lane weights are the live band envelopes over a floor: a finger paints
 * in whatever the music is made of right now, and the floor is what makes it
 * still paint in silence - a finger that left no mark on a quiet passage would
 * read as the touch being broken rather than as the track being quiet.
 *
 * SUMMED over fingers, then capped. Summing is what lets two fingers crossing
 * read brighter than one; the cap is what keeps five of them from writing a
 * value the feedback would carry for the rest of the session. It is a hard
 * ceiling rather than a normalization because the step's own `max(prev, add)`
 * means whatever is injected here is the new floor of that texel's history.
 */
vec3 touchInk(vec2 q, float aspect, float span) {
    vec3 acc = vec3(0.0);
    float radius = TOUCH_INK_RADIUS * span;
    vec3 lane = vec3(0.25) + 0.75 * vec3(uBass, uMid, uTreble);
    for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
        if (i >= uTouchCount) break;
        vec4 t = uTouchPoints[i];
        if (t.z <= 0.0) continue;
        vec2 d = q - vec2(t.x * 0.5 * aspect, t.y * 0.5) * span;
        acc += lane * (t.z * exp(-dot(d, d) / (radius * radius)));
    }
    return min(acc, vec3(TOUCH_INK_CAP));
}

/**
 * One texel of dye, one frame on.
 *
 * `prev` - this texel's own previous value - is deliberately unused: an
 * advecting field wants the dye that ARRIVES here, which is a sample taken at
 * the back-traced coordinate, not the dye that was already here. The layer
 * passes it because most steps want it, and a pure fetch nothing reads is dead
 * code any compiler removes.
 */
vec4 simStep(ivec2 texel, ivec2 size, vec4 prev) {
    vec2 uv = simUv(texel);
    float aspect = float(size.x) / float(size.y);
    vec2 q = (uv - 0.5) * vec2(aspect, 1.0) * (3.2 * uFieldScale);

    vec2 v;
    if (uField == 8) {
        v = curlField(q * 1.4, uDrift) * 0.9;
    } else if (uField == 9) {
        v = poleField(q, uB);
    } else {
        // Embed the screen slice in the 3D field: the slab orbits slowly so
        // the projection keeps discovering new structure without resets.
        vec3 p3 = vec3(q, 1.1 * uSlabY);
        p3.xz = mat2(uSlabX, -uSlabY, uSlabY, uSlabX) * p3.xz;
        vec3 v3 = fieldAt(uField, p3, uB);
        float r = max(length(q), 0.3);
        v = v3.xy + uSwirl * v3.z * vec2(-q.y, q.x) / r;
    }
    v = clamp(v, vec2(-4.0), vec2(4.0));

    // Beat impulse: a radial push away from centre, decaying with uBeat.
    v += normalize(q + vec2(1e-4)) * uBeat * 0.35;

    vec2 back = uv - v * uAdvect / vec2(aspect, 1.0);
    // The one fetch in this family that lands between texels, on essentially
    // every texel of every frame - which is why the spec asks for
    // BETWEEN_TEXELS and the state lives in a filterable format rather than
    // the packed one. Quantising this to whole texels would stop the filaments
    // stretching smoothly and start them crawling in texel steps, and because
    // the field feeds itself that error compounds instead of averaging out.
    vec3 dye = simSample(back).rgb;
    // Sanitize the loop: a NaN or runaway would persist forever.
    dye = clamp(dye, vec3(0.0), vec3(8.0));
    dye = mix(dye, vec3(dot(dye, vec3(0.3333))), 0.012); // slow desaturate
    dye *= uDecay;

    vec2 dir = normalize(v + vec2(1e-4));
    vec3 add = strokes(q, dir) * (0.55 * uDrive);

    // The expanding beat ring deposits into the bass lane.
    if (uBeatRing >= 0.0) {
        float ring = exp(-pow((length(q) - uBeatRing) * 9.0, 2.0));
        add.r += ring * uBeat * 0.8;
    }

    // Deliberately NOT scaled by uDrive: a finger is not audio, and a user who
    // has turned Audio drive down to watch the flow on its own still expects
    // the thing they are touching to answer.
    if (uTouchCount > 0) add += touchInk(q, aspect, 3.2 * uFieldScale);

    vec3 color = max(dye, add) + add * 0.3;
    return vec4(min(color, vec3(8.0)), 1.0);
}
