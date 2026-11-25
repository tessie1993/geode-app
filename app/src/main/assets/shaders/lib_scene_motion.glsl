// The motion uniforms every style may still name. All are CONSTANT but the two flow clocks.
//
// The CPU-side motion field that used to compute them (relative band levels, brightness, a hue driver, a bar
// wave, an orbit, a drift angle, a breath and the flow clocks) is gone, and so is the Note & key layer behind
// the uKnob array: the music no longer moves a picture through them. ShaderScene::uploadMotion holds each
// uniform below at a fixed value, except uFlowPhase and uFlowTime, which advance at one fixed base rate
// (never for loudness), so a style that still reads one draws a steady, finite number, never garbage. A style moves on the sound only through what it reads from lib_sound
// (SND_*, sndShaped(SND_LOW) and its siblings, the SND_*_PRESENCE trends) and through the reaction routes
// the user can see and edit.
//
// Do not add a read of anything below: delete the ones a style has. A style that includes this file and
// reads nothing from it costs nothing: the linker drops the unread uniforms and the uploads become no-ops.
// Includes this AFTER lib_scene_uniforms (it reads uTime) and before lib_palette.

// ---- relative levels, timbre and key (constants) ---------------------------

/** Held at 1: the typical loudness the relative level used to be measured against. */
uniform float uEnergyRel;
/** Held at 0. */
uniform float uMotionBright;
/** Held at 0.5. */
uniform float uHarmony;
/** Held at 0: no hue driver. */
uniform float uKeyHue;
/** Held at 0: the hue driver is worth nothing, so uKeyHue's effect is gated off. */
uniform float uKeyStrength;

// ---- bar wave, orbit, drift, breath (constants) ----------------------------

/** Held at 0.5, the wave's middle. */
uniform float uBarOsc;
/** Held at (0, 0). */
uniform vec2 uOrbit;
/** Held at 0: nothing turns a style on its own. */
uniform float uDrift;
/** Held at 1: no scale wobble. */
uniform float uBreath;

/**
 * A steady travel phase in seconds, advancing at one fixed base rate (0.08 a second) whether the music plays
 * or not; no loudness term. It never runs backwards or stalls.
 */
uniform float uFlowPhase;

/**
 * uFlowPhase at Speed's rate: the same base rate times Speed, integrated (core/viz/modulation/SpeedClock.hpp),
 * so it equals uFlowPhase at Speed 1. Read this, never uFlowPhase times Speed: an accumulated phase times the
 * live dial jumps by phase * delta-speed whenever Speed moves (tools/check_shaders.py fails the build on it).
 */
uniform float uFlowTime;

/**
 * The sound engine's bass, mid and treble clocks (SND_BASS_TIME, SND_MID_TIME,
 * SND_TREBLE_TIME) at Speed's rate, the same way: their per-frame advance
 * times Speed, integrated. Read this, never SND_*_TIME times Speed.
 */
uniform vec3 uSoundTimeSped;

// ---- legacy: constant; delete the readers, then these --------------------
//
// The spike-triggered reaction this uniform group used to carry. Nothing
// writes these any more; ShaderScene uploads the neutral constant every
// frame so a style that still reads them compiles
// and draws exactly the picture it did with a silent, un-spiking input.

/**
 * Unit vector, the current direction of travel. Held at (1, 0): nothing turns
 * it any more.
 */
uniform vec2 uMoveDir;

/** 0..1, held at 0: nothing re-rolls the spawn identity any more. */
uniform float uSpawnSeed;

/** Seconds since uSpawnSeed was last rolled. Held at 1000, well past spawnGrow()'s horizon, so a spawn always reads as fully grown in. */
uniform float uSpawnAge;

/** 0..1, the "which fractal" dial. Held at 0: nothing sends it to a new plateau any more. */
uniform float uFormPhase;

// ---- helpers ---------------------------------------------------------------

/** 0 at the instant of a spawn, 1 once it has grown in over `seconds`. */
float spawnGrow(float seconds) {
    return smoothstep(0.0, max(seconds, 1e-3), uSpawnAge);
}

/** 1 at the instant of a spawn, decaying to 0: the complement of spawnGrow. */
float spawnFresh(float seconds) {
    return 1.0 - spawnGrow(seconds);
}

/** The travel offset a field should be advected by, in style units. */
vec2 flowOffset(float rate) {
    return uMoveDir * (uFlowPhase * rate);
}

/**
 * Rotation matrix aligned with the current direction of travel, for styles
 * that turn a whole structure rather than sliding it.
 */
mat2 flowBasis() {
    return mat2(uMoveDir.x, -uMoveDir.y, uMoveDir.y, uMoveDir.x);
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
 * the whole field drifts the way a spike last pointed it.
 */
vec2 curlVelocity(vec2 p, float scale) {
    vec2 q = p * scale + flowOffset(0.35);
    const float e = 0.09;
    float dx = motionFbm(q + vec2(e, 0.0)) - motionFbm(q - vec2(e, 0.0));
    float dy = motionFbm(q + vec2(0.0, e)) - motionFbm(q - vec2(0.0, e));
    return vec2(dy, -dx) / (2.0 * e);
}

/**
 * Advects `p` through the curl field. `amount` in style units.
 */
vec2 fluidWarp(vec2 p, float scale, float amount) {
    return p + curlVelocity(p, scale) * amount * (0.7 + 0.5 * SND_INTENSITY);
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
    float seed = motionHash(cell + floor(uSpawnSeed * 64.0));
    // Scatter each mote inside its cell and let it orbit slowly, so the layer
    // reads as drifting particles rather than as a lit grid.
    float phase = uFlowPhase * (0.4 + seed) + seed * 6.2831853;
    local += 0.3 * vec2(cos(phase), sin(phase * 1.13));
    float mote = smoothstep(size, size * 0.15, length(local));
    // Only some cells carry one, and the brightest ride the treble.
    float alive = step(0.62, seed);
    return mote * alive * (0.35 + 0.65 * seed) * (0.5 + 0.9 * SND_TREBLE_PRESENCE) * spawnGrow(0.9);
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
 * The whole field drifts along the current travel direction, so a spike
 * re-aims the flow rather than restarting it.
 */
vec3 fluidWarp3(vec3 p, float scale, float amount) {
    vec3 drift = vec3(uMoveDir * (uFlowPhase * 0.30), uFlowPhase * 0.18);
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
