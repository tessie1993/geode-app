// The continuous audio-motion layer every style shares.
//
// WHY THIS EXISTS
//
// lib_scene_uniforms hands a style the RAW band envelopes: uBass, uMid,
// uTreble, uEnergy, uBeat. They are honest, and they are the wrong thing to
// drive a picture with directly. A band envelope can move 0 -> 1 between two
// frames, so anything multiplied by one moves the same way: that is the
// flashing and the snapping. A fragment shader cannot fix it, because a
// fragment shader has no frame-to-frame state and so cannot smooth anything.
//
// So the smoothing lives on the CPU, in viz/MotionField.cpp, and arrives here
// as uniforms. Everything below is already slew-limited or already
// integrated. Reach for these first; reach for the raw envelopes only when a
// style genuinely wants the instantaneous value itself.
//
// A style includes this AFTER lib_scene_uniforms (it reads uTime) and before
// lib_palette. A style that includes it and reads nothing from it costs
// nothing: the linker drops the unread uniforms and the uploads become no-ops.
//
// WAVE THREE: A CONTINUOUS MOTION SYSTEM, NOT A REACTION LAYER
//
// Nothing below is a trigger, and nothing below is keyed off a drum, an
// instrument, a transient, an onset or a beat/downbeat flag. Movement is
// continuous: relative band levels against a running average with
// attack/release (uEnergyRel/uBassRel/uMidRel/uTrebRel), spectral brightness
// and harmonicity (uMotionBright/uHarmony), a chroma-derived key hue
// (uKeyHue/uKeyStrength), tempo phase as smooth phase-locked oscillators
// gated by confidence (uBeatOsc/uBarOsc), and slow re-targeting from novelty
// and section boundaries that eases over seconds (uOrbit/uDrift/uBreath). See
// core/viz/MotionField.hpp for the derivation of every uniform below.

// ---- slew-limited band envelopes -------------------------------------------
//
// Same range and meaning as uBass/uMid/uTreble/uEnergy (0..1.5, auto-gained),
// but no single frame can move one of them far. Substituting uBassSmooth for
// uBass is the one-line way to take the flash out of an existing style.
uniform float uBassSmooth;
uniform float uMidSmooth;
uniform float uTrebleSmooth;
uniform float uEnergySmooth;

/**
 * The slowest signal available: how loud this PASSAGE is, not what just
 * happened in it. Takes about a second to arrive and longer to leave, so it is
 * what to breathe a scale, a density or a fog depth with.
 */
uniform float uSwell;

// ---- wave three: relative levels, timbre and key --------------------------

/** rms / 20s running average, attack 0.25s / release 1.0s; 0..2, 1 = typical loudness for this track. */
uniform float uEnergyRel;
/** bass / 20s running average, attack 0.15s / release 0.6s; 0..2. */
uniform float uBassRel;
/** mid / 20s running average, attack 0.15s / release 0.6s; 0..2. */
uniform float uMidRel;
/** treble / 20s running average, attack 0.15s / release 0.6s; 0..2. */
uniform float uTrebRel;
/** Spectral centroid, smoothed over 0.5s; 0 dark, 1 bright. */
uniform float uMotionBright;
/** Harmonicity, smoothed over 1.0s; 0 noisy/percussive, 1 tonal. */
uniform float uHarmony;
/** The chroma argmax's hue, 0..1, circularly eased over 3.0s. */
uniform float uKeyHue;
/** How confident the key detector is right now, smoothed over 1.0s, 0..1; gates uKeyHue's effect. */
uniform float uKeyStrength;

// ---- wave three: tempo phase, gated by confidence --------------------------

/** 0.5 + 0.5*sin(2*pi*beatPhase) scaled by rhythmLock (pulseConfidence*tempoStability, slewed 1s); 0.5 when unlocked. */
uniform float uBeatOsc;
/** Same construction as uBeatOsc, from the bar phase instead of the beat phase. */
uniform float uBarOsc;
/** A slow wander point, -1..1; re-targeted (not stepped) on a novelty rise or a section boundary and eased over 3.0s. */
uniform vec2 uOrbit;
/** An accumulated rotation, radians; its sign eases rather than flips across a section. */
uniform float uDrift;
/** A slow, low-amplitude scale wobble around 1.0, 0.9..1.1, riding uBassRel and uBarOsc. */
uniform float uBreath;
/** The Reactivity tab's Motion amount dial (SceneParams.motionAmount), 0..1, verbatim. */
uniform float uMotion;

/**
 * A monotonically advancing travel phase in seconds. Loudness sets the RATE
 * (via uEnergyRel on the CPU), never the sign, so anything scrolled or
 * advected by this can slow down and speed up but can never run backwards or
 * stall into a stutter.
 */
uniform float uFlowPhase;

// ---- helpers ---------------------------------------------------------------
//
// R08 removed the spike-triggered uniform group this section used to read
// (uSpike, uMoveDir, uSpawnSeed, uSpawnAge, uFormPhase - ShaderScene held
// them at a neutral constant since wave three, so this is a behaviour-
// preserving rewrite, not a new recipe). Every helper below keeps its old
// signature so R02-R07's callers keep compiling; each now derives its
// answer from a continuous uniform instead.

/** Wave three: the current direction of travel, from the CPU's slow accumulated rotation (uDrift) instead of a spike-set heading. Always unit length. */
vec2 moveDir() {
    return vec2(cos(uDrift), sin(uDrift));
}

/**
 * 0 at the instant of a spawn, 1 once it has grown in over `seconds`. Wave
 * three: nothing re-spawns any more (the CPU held the old uSpawnAge at 1000,
 * far past every caller's horizon), so this already always read as fully
 * grown in - the constant below is that same steady state, not a new one.
 */
float spawnGrow(float seconds) {
    return 1.0;
}

/** 1 at the instant of a spawn, decaying to 0: the complement of spawnGrow. */
float spawnFresh(float seconds) {
    return 1.0 - spawnGrow(seconds);
}

/** The travel offset a field should be advected by, in style units. */
vec2 flowOffset(float rate) {
    return moveDir() * (uFlowPhase * rate);
}

/**
 * Rotation matrix aligned with the current direction of travel, for styles
 * that turn a whole structure rather than sliding it.
 */
mat2 flowBasis() {
    vec2 d = moveDir();
    return mat2(d.x, -d.y, d.y, d.x);
}

/**
 * A cheap 2D value-noise field, curl-differenced into a divergence-free
 * velocity. This is the fluid in "fluid look" for a style that has no solver:
 * it swirls and folds like advected dye and, being divergence-free, it never
 * piles material up into the hard bright knots a plain noise warp produces.
 *
 * Deliberately in-shader rather than a read of uFlow. The composite pass
 * already warps EVERY style through the shared FlowField (see the note in
 * winter_frag), so a second read of that same texture here would apply one
 * velocity field twice.
 */
float motionHash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float motionNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(motionHash(i), motionHash(i + vec2(1.0, 0.0)), f.x),
               mix(motionHash(i + vec2(0.0, 1.0)), motionHash(i + vec2(1.0, 1.0)), f.x), f.y);
}

/** Two octaves is enough to read as fluid and cheap enough for a per-pixel call. */
float motionFbm(vec2 p) {
    return 0.62 * motionNoise(p) + 0.38 * motionNoise(p * 2.17 + 11.3);
}

/**
 * Divergence-free velocity at `p`. `scale` is the eddy size in style units;
 * larger is coarser. Already advected along the current travel direction, so
 * the whole field drifts the way uDrift is currently pointed.
 */
vec2 curlVelocity(vec2 p, float scale) {
    vec2 q = p * scale + flowOffset(0.35);
    const float e = 0.09;
    float dx = motionFbm(q + vec2(e, 0.0)) - motionFbm(q - vec2(e, 0.0));
    float dy = motionFbm(q + vec2(0.0, e)) - motionFbm(q - vec2(0.0, e));
    return vec2(dy, -dx) / (2.0 * e);
}

/**
 * Advects `p` through the curl field. `amount` in style units; a louder-
 * than-average passage widens the eddies through uEnergyRel, continuously,
 * rather than a hit displacing anything instantly.
 */
vec2 fluidWarp(vec2 p, float scale, float amount) {
    return p + curlVelocity(p, scale) * amount * (0.7 + 0.5 * uSwell + 0.3 * clamp(uEnergyRel - 1.0, 0.0, 1.0));
}

/**
 * Sparse bright motes riding the curl field: the particle half of "fluid and
 * fluid particle flow". Returns an additive intensity, already faded in with
 * the current spawn so a new one does not pop.
 *
 * `density` cells across the plane, `size` the mote radius in cell units.
 */
float fluidMotes(vec2 p, float density, float size) {
    vec2 q = fluidWarp(p, 1.4, 0.22) * density + flowOffset(0.9) * density * 0.15;
    vec2 cell = floor(q);
    vec2 local = fract(q) - 0.5;
    // Wave three: identity drifts slowly with travel time instead of re-rolling on a hit.
    float seed = motionHash(cell + floor(uFlowPhase * 0.08));
    // Scatter each mote inside its cell and let it orbit slowly, so the layer
    // reads as drifting particles rather than as a lit grid.
    float phase = uFlowPhase * (0.4 + seed) + seed * 6.2831853;
    local += 0.3 * vec2(cos(phase), sin(phase * 1.13));
    float mote = smoothstep(size, size * 0.15, length(local));
    // Only some cells carry one, and the brightest ride the treble.
    float alive = step(0.62, seed);
    return mote * alive * (0.35 + 0.65 * seed) * (0.5 + 0.9 * uTrebleSmooth) * spawnGrow(0.9);
}

// ---- the 3D half: a divergence-free flow a raymarch can afford --------------
//
// curlVelocity() above differentiates a noise field, which costs four fbm
// evaluations. That is fine once per pixel and ruinous inside a march loop,
// where map() runs up to 128 times per ray.
//
// So the 3D field is analytic instead: the Arnold-Beltrami-Childress flow, a
// steady solution of the Euler equations. Each component depends only on the
// OTHER two coordinates, so every term of the divergence is identically zero -
// it is exactly incompressible by construction, not approximately so, and it
// costs six trig calls rather than four fbm evaluations.
//
// Being divergence-free is what makes it look like a fluid rather than like a
// noise warp: incompressible flow folds and shears material without ever
// compressing it into the hard bright knots a plain gradient displacement
// produces.
vec3 abcFlow(vec3 p) {
    return vec3(sin(p.z) + cos(p.y),
                sin(p.x) + cos(p.z),
                sin(p.y) + cos(p.x));
}

/**
 * Advects a point through the flow. `scale` is the eddy size (larger is
 * finer), `amount` the displacement in world units.
 *
 * Two applications rather than one: a single pass of a steady field only bends
 * space, while feeding the result back in folds it, which is what turns a
 * smooth swirl into something that reads as stirred.
 *
 * The whole field drifts along the current travel direction, so uDrift
 * re-aims the flow continuously rather than a spike restarting it.
 */
vec3 fluidWarp3(vec3 p, float scale, float amount) {
    vec3 drift = vec3(moveDir() * (uFlowPhase * 0.30), uFlowPhase * 0.18);
    vec3 q = p + amount * abcFlow(p * scale + drift);
    return q + amount * 0.55 * abcFlow(q * scale * 2.07 + drift * 1.4 + 3.1);
}

/**
 * Upper bound on fluidWarp3's Jacobian norm - divide a distance estimate
 * measured in warped space by this before stepping a ray, exactly as
 * touchWarpLipschitz() is used.
 *
 * WHY THIS NUMBER. The Jacobian of abcFlow has two non-zero entries per row,
 * each a sine or cosine and so at most 1 in magnitude, giving a row-sum
 * (infinity) norm of at most 2. One application of `p + a * abcFlow(p * s)`
 * therefore has norm at most 1 + 2*a*s, and the second application at 0.55 the
 * amplitude and 2.07 the scale composes multiplicatively.
 *
 * It is a bound, not the true norm, so it is conservative: the march takes
 * shorter steps than it strictly must. That is the correct direction to be
 * wrong in - an overestimated step walks the ray through the surface, which is
 * the one failure a distance march cannot recover from.
 */
float fluidWarp3Lipschitz(float scale, float amount) {
    return (1.0 + 2.0 * amount * scale) * (1.0 + 2.0 * 0.55 * amount * 2.07 * scale);
}
