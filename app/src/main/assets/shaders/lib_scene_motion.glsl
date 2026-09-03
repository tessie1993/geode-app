// The smoothed audio-motion layer every style shares.
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
// So the smoothing lives on the CPU, in ShaderScene::stepMotion, and arrives
// here as uniforms. Everything below is already slew-limited or already
// integrated. Reach for these first; reach for the raw envelopes only when a
// style genuinely wants the transient itself.
//
// A style includes this AFTER lib_scene_uniforms (it reads uTime) and before
// lib_palette. A style that includes it and reads nothing from it costs
// nothing: the linker drops the unread uniforms and the uploads become no-ops.
//
// WHAT A SPIKE MEANS
//
// A transient does NOT brighten, jump or shake the frame. It picks a new
// DESTINATION, and the value the style reads travels there over most of a
// second. There are three destinations, and they are the three things a spike
// is allowed to mean:
//
//   uMoveDir    a new direction of travel   (bounded turn, never a reversal)
//   uSpawnSeed  a new spawn                 (with uSpawnAge to grow it in)
//   uFormPhase  a new fractal               (a new plateau on a smooth walk)
//
// Spikes are rate-limited on the CPU, so a busy drum line re-aims the picture
// a few times a second at most rather than once per frame.

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

/**
 * The transient, as an envelope that RISES (~120ms) and falls (~600ms) rather
 * than stepping. Peaks near 1 on a hit and returns to 0 between them.
 *
 * Safe to key brightness off, unlike uBeat, precisely because it cannot reach
 * its peak inside one frame. Use it for a swelling accent; use uMoveDir /
 * uSpawnSeed / uFormPhase for the change of state the same hit caused.
 */
uniform float uSpike;

// ---- what the last spike decided -------------------------------------------

/**
 * Unit vector, the current direction of travel. Turns to a new bearing when a
 * spike lands and glides there over roughly a second; the turn is bounded well
 * under a half circle, so a field advected along it never appears to reverse.
 */
uniform vec2 uMoveDir;

/** 0..1, re-rolled on a spike. The identity of whatever is on screen now. */
uniform float uSpawnSeed;

/**
 * Seconds since uSpawnSeed was re-rolled. Zero at the instant of the spike,
 * counting up after. Feed it through spawnGrow() so a new spawn fades in
 * instead of appearing.
 */
uniform float uSpawnAge;

/**
 * 0..1, the "which fractal" dial. A spike sends it to a new plateau chosen off
 * a golden-ratio walk - so consecutive spikes always land visibly apart rather
 * than dithering around one value - and it glides there. Never jumps.
 */
uniform float uFormPhase;

/**
 * A monotonically advancing travel phase in seconds. Loudness sets the RATE
 * (via uEnergySmooth on the CPU), never the sign, so anything scrolled or
 * advected by this can slow down and speed up but can never run backwards or
 * stall into a stutter.
 */
uniform float uFlowPhase;

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
 * Advects `p` through the curl field. `amount` in style units; a hit widens
 * the eddies through uSpike rather than displacing anything instantly.
 */
vec2 fluidWarp(vec2 p, float scale, float amount) {
    return p + curlVelocity(p, scale) * amount * (0.7 + 0.5 * uSwell + 0.3 * uSpike);
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
    return mote * alive * (0.35 + 0.65 * seed) * (0.5 + 0.9 * uTrebleSmooth) * spawnGrow(0.9);
}
