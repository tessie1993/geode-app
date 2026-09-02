#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uAudioTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// The three lines every fragment style carries, in this order: the uniform
// block/aband/awave/view() first, then the palette, then grade() - which calls
// pal() and so must come after it.
//#include lib_scene_uniforms
//#include lib_palette
//#include lib_scene_grade
// The 3D toolkit and the touch helpers, already wired so a style author never
// has to edit this header. Both are pure functions over things declared above;
// an unused one is dropped by the linker and costs nothing.
//#include lib_sdf3
//#include lib_touch

// NEBULA - a volumetric fbm cloud, lit from inside.
//
// The only style in this family with no surface in it. Nothing is ever "hit":
// the ray integrates emission and absorption through a participating medium
// and stops when there is nothing left to see through. Beer-Lambert, one slab
// at a time, in CLOSED FORM over the slab rather than as emission times
// length:
//
//     absorbed = 1 - exp(-density * extinction * ds)
//     col     += emission * T * absorbed
//     T       *= 1 - absorbed
//
// The two forms agree to first order in ds, but only this one carries the
// medium's radiance ceiling at ANY step size - which matters here because ds
// is set by the user's Detail control, and a style whose brightness moved with
// a quality slider would be unusable. hyperspace_frag integrates its liquid
// light the same way for the same reason.
//
// The ordering matters too. Emission is deposited with the transmittance the
// ray arrived WITH, before this slab absorbs anything; a slab cannot occlude
// itself. The other way round darkens the near face of every cloud and the
// medium loses its front.
//
// ---- why this cannot flash ------------------------------------------------
//
// The app has a photosensitivity budget, and an emissive volume is exactly the
// kind of thing that blows through one. It cannot here, and the reason is the
// integral rather than a clamp: the weights T * absorbed over a ray sum to
// 1 - T_final <= 1 by construction, so the accumulated radiance is a convex
// combination of the emission values along the ray and can never exceed the
// largest of them. Emission is then soft-clipped at NEB_EMIT_CEILING before it
// enters the sum, which makes that a hard per-pixel bound: no pixel leaves this
// shader brighter than NEB_EMIT_CEILING, whatever the music or the fingers do.
// Louder music cannot brighten the frame past it - it can only reach it sooner.
//
// The one term with real transient energy, the beat shock, is a THIN expanding
// shell: a ring on screen, never a full field, and it fades as it travels.
// Nothing here changes large-area luminance in one frame.

// ---- march budget ---------------------------------------------------------

/** Compile-time ceiling on the volume march. MarchBudget.MAX_STEPS. */
#define NEB_MAX_STEPS 128
/**
 * Fraction of uSteps this style actually spends.
 *
 * uSteps is a budget, not a step count, and one volume step is not one
 * surface step. A marched SDF step costs one distance estimate; a step here
 * costs three warp fbms (6 vnoise3), one density fbm (3 to 5) and, where there
 * is medium, two shadow taps (4) - fifteen lattice noise evaluations at worst,
 * which is over an order of magnitude. Handed the raw budget this style would
 * cost many times its four siblings at the same Detail setting, which is not
 * what the user moving one slider is asking for. So it spends a fixed FRACTION
 * of the budget: Detail still moves the march monotonically (45 steps at
 * Detail 0.25, 90 at 1.5), it just buys volume steps at the volume step's
 * price.
 */
#define NEB_STEP_SCALE 0.7
/** Floor, so a scene that never uploaded uSteps still renders a cloud. */
#define NEB_MIN_STEPS 32.0
/**
 * Smallest slab worth integrating. A ray that only clips the support sphere
 * has a span of almost nothing; without this it would spend its whole budget
 * standing still in a region whose envelope is zero anyway.
 */
#define NEB_MIN_STEP 0.012
/**
 * Stop once the medium in front has swallowed all but this much of the light
 * behind it. THIS is what makes the style affordable: inside a dense filament
 * the ray gives up in the first third of its budget, and those are exactly the
 * pixels where the noise is most expensive. At 0.015 the discarded remainder
 * is under half a grade level, so no edge shows where rays quit.
 */
#define NEB_MIN_TRANSMITTANCE 0.015

// ---- camera ---------------------------------------------------------------

/**
 * Focal length AND eye distance, and they are equal on purpose.
 *
 * With the eye at (0, 0, -D) and rd = normalize(vec3(uv, F)), a point at
 * z = 0 lands at uv * D / F. At D == F that is uv exactly, so the cloud's
 * centre plane maps one-to-one onto view() coordinates - which is the frame
 * lib_touch's helpers are defined in. touchAnchor(), touchFalloff() and
 * touchWarp3() therefore need no correction: the finger's world ray crosses
 * the cloud's middle precisely under the finger. Decoupling these two numbers
 * to widen the field of view would put the touch well visibly off the fingertip
 * and there is no cheap way to correct it, because the anchor comes back in
 * view() units by contract.
 *
 * 3.1 rather than something tighter so the eye stays OUTSIDE the support
 * sphere at every reachable inflation: bass tops the envelope radius out at
 * 1.15, which is a support of 2.15, and a finger down adds 0.14 - 2.29 at the
 * very worst, comfortably short of the lens. Outside, the ray-sphere entry
 * point is a real trim that skips empty space; inside, it would collapse to
 * zero and the march would start at the lens.
 */
#define NEB_FOCAL 3.1
#define NEB_CAM_DIST 3.1

// ---- the cloud's envelope -------------------------------------------------

/** Radius of the Gaussian envelope, in view() units. */
#define NEB_RADIUS 0.95
/**
 * Density below which the envelope is treated as absent.
 *
 * Subtracted from the Gaussian rather than compared against it, so the
 * envelope is EXACTLY zero outside its support instead of merely small. That
 * turns the bounding sphere from an approximation into a proof: no density
 * can exist outside it, so nothing is clipped and no faint shell is left
 * behind at the bound where the march stopped.
 */
#define NEB_ENV_CUTOFF 0.03
/** 1 / (1 - NEB_ENV_CUTOFF), so the centre of the cloud still reads 1. */
#define NEB_ENV_NORM 1.0309278
/**
 * sqrt(-ln(NEB_ENV_CUTOFF)) = 1.87256..., rounded UP.
 *
 * exp(-r^2/R^2) drops below the cutoff at r = R * this, so R * this is the
 * exact support radius. Rounded up rather than to nearest: rounding down would
 * put the bounding sphere a hair inside the support and shave a one-pixel ring
 * off the cloud's edge.
 *
 * The cutoff is 3%, not the 0.35% an earlier draft used, and the reason is
 * step SIZE rather than step count. The march divides the span it is given by
 * the budget, so a support radius chosen to include the envelope's last
 * thousandth stretches every slab across a span that is mostly empty tail -
 * and a slab longer than a filament is thick integrates a dense sample as one
 * lump, which the ray-start dither then turns into visible grain. Cutting the
 * tail at 3% shortens the span by a fifth, buys back that much resolution at
 * every Detail setting, and costs a boundary whose density was already under
 * a hundredth of the core's.
 */
#define NEB_SUPPORT_K 1.8726

// ---- the density field ----------------------------------------------------

/**
 * The warp field is sampled at 0.32x the density field's scale, which is what
 * makes it a DOMAIN WARP rather than more noise: the field that displaces has
 * to be much longer-waved than the field being displaced, or the two just add
 * and the result is fog again. At this ratio the warp drags whole regions of
 * the density field sideways and the cloud grows filaments and voids instead
 * of an even speckle.
 *
 * The density scale is set by the FIRST octave, not the last. fbm3 halves its
 * amplitude each octave, so the base octave carries two thirds of the field's
 * variance and everything above it is trim - which means the base octave's
 * wavelength IS the size of a filament. At 5.0 that is 0.2 world units, about
 * a twelfth of the cloud. The first version of this shader used 2.1, giving a
 * base octave two cells wide across the whole cloud, and no threshold or gain
 * could rescue it: the picture was correctly integrated, correctly lit fog.
 */
#define NEB_WARP_SCALE 1.6
#define NEB_DENSITY_SCALE 5.0
/** Two octaves: the warp only needs shape, and its detail is invisible under a 3x finer field. */
#define NEB_WARP_OCTAVES 2
/**
 * Displacement in density-field units. Over one full cell, so a filament can
 * be pulled clear across the cell it started in - below about 0.7 the warp
 * reads as a wobble and the cloud stays isotropic.
 */
#define NEB_WARP_AMP 1.6
/** Detail near the eye. Held under FBM3_MAX_OCTAVES; the sixth octave is finer than a pixel here. */
#define NEB_MAX_OCTAVES 5
/** Detail at the far side of the volume - see the LOD note in the march. */
#define NEB_MIN_OCTAVES 3
/** The shadow tap only needs the mass, not the filigree. */
#define NEB_SHADOW_OCTAVES 2

/**
 * Noise below this is empty space, and it is the single most important number
 * in the file.
 *
 * fbm3 is a sum of value noise, so it is not uniform on [0,1]: it piles up
 * around 0.5 with a standard deviation near 0.10 at five octaves. That makes
 * the threshold a position in the TAIL, not a midpoint. At 0.56 - a little
 * over half a sigma out - roughly a quarter of the volume survives, which is
 * what a nebula is: mostly nothing, with strands in it. Thresholding near the
 * mean instead keeps half the volume as a continuous haze and the result is
 * fog no amount of lighting can rescue.
 *
 * It also pays for itself. Three quarters of the samples come out at exactly
 * zero density and skip the lighting branch entirely.
 */
#define NEB_THRESHOLD 0.56
/**
 * Floor on the threshold once the touch and beat terms have pushed it down.
 *
 * Those terms sum to 0.24 and the floor only allows 0.20 of it, so the floor
 * BINDS when a finger, a bridge and a beat land on the same sample - which is
 * the point. Each of them is meant to be visible on its own; all three at once
 * would drop the bar far enough that the mask stops masking and the support
 * sphere fills in as a solid ball, and that reads as the cloud being deleted
 * rather than gathered.
 */
#define NEB_MIN_THRESHOLD 0.36
/**
 * Turns the surviving tail into an optical density of order 1.
 *
 * Large because the tail it multiplies is small: the mean of max(f - 0.56, 0)
 * is about 0.018, so the gain and the extinction together have to supply the
 * two orders of magnitude. Gain and extinction are not interchangeable - gain
 * changes the CONTRAST between a strand and its edge, extinction changes how
 * far the ray sees.
 */
#define NEB_DENSITY_GAIN 8.0
/**
 * Optical depth per unit density per world unit.
 *
 * Opacity only - in the closed form above it says nothing about brightness,
 * which is what makes it tunable on its own. What it really sets is HOW MANY
 * LAYERS DEEP the eye sees, and that turned out to be the difference between a
 * nebula and fog. The cloud is about twelve filaments thick along a central
 * ray; at 5.5 the ray averaged all twelve and every strand dissolved into the
 * mean, which is a soft wash no amount of lighting fixes. At 13 the front two
 * or three dominate, the ones behind them shade the ones in front, and the
 * core lamp reads as a glow BEHIND something rather than a bright patch.
 * Higher again and the volume collapses into a lit shell with no interior.
 */
#define NEB_EXTINCTION 13.0

// ---- lighting from inside -------------------------------------------------

/**
 * Taps toward the core per marched step. Two, not a second march.
 *
 * A real shadow ray would multiply the cost of the whole frame by the length
 * of the light path. Instead the light is split in two: these taps carry the
 * LOCAL relief - the near shoulder of a filament being brighter than its far
 * one, which is what makes the strands look solid - while everything beyond
 * their reach is handled analytically by NEB_CORE_SHADOW below, which costs
 * nothing per unit distance. Two taps is where the relief stops improving
 * visibly; a third only deepened creases already dark.
 */
#define NEB_LIGHT_TAPS 2
/** Reach of one tap, in world units - about a fifth of the cloud radius. */
#define NEB_LIGHT_STEP 0.22
/** Once this little light survives the taps, the rest cannot change the pixel. */
#define NEB_LIGHT_CUTOFF 0.02
/** Inverse-square-ish falloff of the core lamp. At r = 1 the core delivers a quarter. */
#define NEB_CORE_FALLOFF 3.0
/**
 * Long-range self-shadowing: optical depth from the core out to r, per unit
 * of the saturating column integral below.
 *
 * The column of medium between a sample and the core is the envelope
 * integrated along the radius times the mean of the masked noise, and the
 * mean is a constant - the noise averages out over a path that long. So the
 * whole of it is a closed-form function of r, and the far side of the cloud
 * goes dark for free. This is the term that gives the volume an inside.
 *
 * Gentle, because it is the third sub-1 factor the core lamp is multiplied by
 * (geometry, then this, then the two local taps). Three aggressive falloffs
 * multiply out to a cloud that is black everywhere but its own middle; the
 * budget for "dark" has to be split between them, not spent three times.
 */
#define NEB_CORE_SHADOW 1.1
/**
 * Slope of the saturating approximation to that integral.
 *
 * radius * (1 - exp(-K * r / radius)) has the shape of the true erf-based
 * column - linear at the centre, saturating at a fixed multiple of the radius
 * once the ray has left the cloud - for one exp instead of an erf that GLSL
 * ES does not have. K is set so the two agree over the first radius, where
 * all the visible shading is.
 */
#define NEB_COLUMN_K 1.35

// ---- emission -------------------------------------------------------------

/**
 * All emission below is in RADIANCE units, straight out of the closed-form
 * slab: the number a pixel would show if the whole ray sat at that value. So
 * these read directly as screen brightness before grade(), which is the point
 * of integrating in closed form rather than as emission * density * ds - in
 * that form every one of these would have to be divided by the extinction in
 * the author's head to mean anything.
 */

/**
 * Emission the medium has with no light reaching it.
 *
 * Not zero: the outer skirts of a nebula are lit by things this shader is not
 * modelling, and at zero the frame's edges fall to black and the cloud reads
 * as a lamp in a box rather than as something the room is full of.
 */
#define NEB_AMBIENT_EMIT 0.22
/** The core lamp. The largest term by far - "lit from inside" is the whole premise. */
#define NEB_CORE_EMIT 4.5
/**
 * Where the sum is soft-clipped, and the hard per-pixel ceiling of this style.
 *
 * e / (1 + e / C) rather than min(e, C): a hard clip would flatten the core
 * into a disc of one value the moment two terms coincided there, and the
 * flattening would appear and disappear on beats, which is exactly the
 * large-area luminance step the flash budget exists to prevent. The soft knee
 * keeps the gradient alive all the way up and still bounds the sum at C -
 * see the flash note at the top of the file.
 */
#define NEB_EMIT_CEILING 1.8
/**
 * Where treble lights the field: the top of the tail that already passed the
 * density mask, so the veins are thin bright lines THROUGH the strands rather
 * than a brightening of them.
 *
 * The window is narrow (0.16 wide, against a field whose whole standard
 * deviation is 0.10) for the same reason: ramped over the full 0.62..1.0 the
 * smoothstep never gets near 1, because fbm3 essentially never reaches 1, and
 * treble did nothing visible at all.
 */
#define NEB_VEIN_THRESHOLD 0.62
#define NEB_VEIN_TOP 0.78
#define NEB_VEIN_EMIT 1.9
/**
 * How much of the vein term survives silence.
 *
 * Not zero. The veins are the only thing in this style that makes a strand's
 * spine brighter than its shoulder, and without them an integral through a
 * dozen overlapping strands averages into the soft wash this shader spent a
 * draft being. Treble ADDS to this rather than switching it on, which is also
 * what the audio convention asks for: treble is fine detail and sparkle, not
 * the existence of detail.
 */
#define NEB_VEIN_BASE 0.55
/** Peak brightness of the beat shell. */
#define NEB_SHOCK_EMIT 0.8
/** World units the shell travels over one beat phase - just past the support radius. */
#define NEB_SHOCK_SPEED 2.6
/** Shell half-width. Thin enough to read as a front, wide enough to survive a 0.1 step. */
#define NEB_SHOCK_WIDTH 0.17
/** How much the passing front compresses the medium ahead of it. */
#define NEB_SHOCK_THICKEN 0.10

/**
 * Global glow floor and gain on uEnergy. The floor is 1.0, not something less,
 * because energy has to be able to add without silence having to look dim -
 * a wallpaper spends most of its life at uEnergy 0.
 */
#define NEB_ENERGY_BASE 1.0
#define NEB_ENERGY_GAIN 0.35

// ---- audio ----------------------------------------------------------------

/**
 * Bass inflates the cloud and deepens it.
 *
 * Both gains are small, and that is deliberate rather than timid: this shader
 * has no frame-to-frame state, so it cannot smooth anything itself. uBass is
 * already an auto-gained envelope, but any jitter left in it lands straight
 * on the geometry, and a cloud whose RADIUS flickers is far more obvious than
 * one whose brightness does. Big audio gestures belong on the emission terms,
 * which are forgiving; geometry gets a light touch.
 */
#define NEB_BASS_INFLATE 0.14
#define NEB_BASS_EXTINCT 0.35
/**
 * Mids steer the churn: they rotate the noise domain, and only that.
 *
 * NOT a rate multiplied by uTime. `f(audio) * uTime` is the classic trap -
 * nudging the multiplier teleports the field by elapsed * delta, which after
 * a few minutes is the whole cloud jumping on a cymbal. An angle is safe: it
 * is continuous in the audio value, it returns when the music does, and at
 * silence it is exactly zero.
 */
#define NEB_MID_STEER 0.55
/** Ambient tumble, radians per second. Slow enough to be felt rather than watched. */
#define NEB_TUMBLE_Y 0.017
#define NEB_TUMBLE_X 0.011

// ---- touch ----------------------------------------------------------------

/** Reach of the anchor well, in view() units. */
#define NEB_TOUCH_RADIUS 0.32
/**
 * How far the finger drops the density threshold under itself.
 *
 * The well works by lowering the BAR the noise has to clear, not by adding a
 * blob of density. Lowering the threshold lets more of the field's own
 * structure through, so the cloud thickens with the filaments it already had
 * and the gather reads as the medium collecting rather than as a sphere being
 * pasted over it.
 */
#define NEB_WELL_THRESHOLD 0.06
/** Emission the fading slots leave behind, after the wake sum is tone-mapped. */
#define NEB_WAKE_EMIT 0.45
/** Half-width of the two-finger corridor. */
#define NEB_CORRIDOR_RADIUS 0.20
#define NEB_BRIDGE_THRESHOLD 0.08
#define NEB_BRIDGE_EMIT 0.8
/** Radians of swirl per unit of uTouchSpin, and how tightly it hugs the view axis. */
#define NEB_SPIN_GAIN 0.9
#define NEB_SPIN_TIGHT 0.55
/**
 * How far outside the support sphere the march must start once a finger is
 * down, in view() units.
 *
 * touchWarp3 pulls a point toward the anchor by |d| * G * exp(-|d|^2/R^2)
 * with G = TOUCH_WARP_GAIN (0.45) and R = TOUCH_WARP_RADIUS (0.7). That
 * displacement peaks at |d| = R/sqrt(2) = 0.495 and is worth
 * 0.45 * 0.495 * exp(-0.5) = 0.1351 - so a point up to 0.1351 OUTSIDE the
 * support sphere can be dragged inside it and acquire density. Without this
 * margin the finger would shave a crescent off the cloud's far edge. Scaled
 * by the anchor strength, so an untouched frame adds exactly zero and marches
 * the same span it always did.
 */
#define NEB_TOUCH_MARGIN 0.14

// ---- colour ---------------------------------------------------------------

/**
 * Where the palette is read. Everything the eye calls "colour" here is one
 * pal() lookup driven by density, depth and proximity to the core, so the
 * user's palette, hue range, LUT and colour-cycle controls all mean what they
 * mean everywhere else. The total swing is held under half a turn so the
 * cloud reads as one body with a hot centre, not as a rainbow.
 */
#define NEB_HUE_BASE 0.08
#define NEB_HUE_DENSITY 0.30
#define NEB_HUE_DEPTH 0.22
#define NEB_HUE_CORE 0.30
#define NEB_HUE_MID 0.07
/** Density that counts as fully dense for hue purposes. */
#define NEB_HUE_DENS_NORM 0.9

/** Deep space behind the cloud. Dim, but never black - a dead frame reads as a crash. */
#define NEB_SKY_LEVEL 0.09
#define NEB_SKY_HUE 0.62
#define NEB_SKY_TILT 0.09

/** Corner falloff. A floor rather than a fade to zero, so wide aspects keep their corners. */
#define NEB_VIGNETTE_FLOOR 0.55
#define NEB_VIGNETTE_INNER 0.25
#define NEB_VIGNETTE_OUTER 2.0

/**
 * The cloud's envelope at squared radius rSq.
 *
 * Takes the SQUARE so the caller can skip a sqrt, and so the light taps -
 * which walk straight down the radius toward the core - can pass
 * (r - lt) * (r - lt) instead of building a point and measuring it.
 */
float nebEnvelope(float rSq, float radius) {
    return max(exp(-rSq / (radius * radius)) - NEB_ENV_CUTOFF, 0.0) * NEB_ENV_NORM;
}

/**
 * The domain-warped coordinate the density field is read at.
 *
 * Three fbm samples of one coarse field at decorrelating offsets, used as a
 * displacement vector. The offsets are large and mutually irrational-looking
 * because fbm3 already offsets its own octaves by a fixed vector: two
 * components sampled a few cells apart would share the same octave
 * structure and the displacement would collapse onto a plane, which shows up
 * as a cloud combed flat in one direction.
 */
vec3 nebWarpCoord(vec3 p) {
    vec3 pw = p * NEB_WARP_SCALE;
    vec3 w = vec3(
        fbm3(pw, NEB_WARP_OCTAVES),
        fbm3(pw + vec3(23.7, 8.3, 47.1), NEB_WARP_OCTAVES),
        fbm3(pw + vec3(5.9, 63.7, 13.1), NEB_WARP_OCTAVES)
    ) - 0.5;
    return p * NEB_DENSITY_SCALE + w * NEB_WARP_AMP;
}

void main() {
    // view() first, so Zoom, Rotation, Drift, Kaleidoscope, Tile, Pixelate,
    // Shake, Twist, Warp, Ripple, Morph, Turbulence and the beat pulse all
    // steer this style exactly as they steer every other one. The ray is built
    // out of what it returns, so those controls move the CAMERA rather than
    // smearing an image afterwards.
    vec2 uv = view();

    vec3 ro = vec3(0.0, 0.0, -NEB_CAM_DIST);
    vec3 rd = normalize(vec3(uv, NEB_FOCAL));

    // ---- what the music is doing to the medium ----------------------------
    // Clamped at 1.5 because that is the documented top of the auto-gained
    // range; a transient past it would push the cloud outside its own
    // bounding sphere, which the march would then clip.
    float bass = clamp(uBass, 0.0, 1.5);
    float mid = clamp(uMid, 0.0, 1.5);
    float treble = clamp(uTreble, 0.0, 1.5);
    float energy = clamp(uEnergy, 0.0, 1.5);
    float beatEnv = clamp(uBeat, 0.0, 1.0);

    float radius = NEB_RADIUS * (1.0 + NEB_BASS_INFLATE * bass);
    float extinction = NEB_EXTINCTION * (1.0 + NEB_BASS_EXTINCT * bass);
    // Self-shadowing tracks extinction: bass that thickens the medium has to
    // darken its interior too, or the cloud gets denser and brighter at once
    // and stops reading as a volume.
    float extRatio = extinction / NEB_EXTINCTION;
    float glowGain = NEB_ENERGY_BASE + NEB_ENERGY_GAIN * energy;

    // The beat shell. A DISCRETE event: uBeatPhase resets to 0 on a heard
    // transient, so the shell is born at the core on the hit and its radius is
    // the phase ramp. Its brightness rides the SQUARED beat envelope - the
    // same gate the house beat-bump idiom uses - so the free-running phase
    // clock cannot keep launching shells through silence. Fades with
    // (1 - phase) so it dissipates rather than reaching the edge and stopping.
    float shockAmp = beatEnv * beatEnv * clamp(uBeatResponse, 0.0, 2.0) * (1.0 - uBeatPhase);
    float shockR = uBeatPhase * NEB_SHOCK_SPEED;

    // Ambient life. Both of these run at a constant rate off uTime, which is
    // already ShaderScene's speed-INTEGRATED clock - multiplying by uSpeed
    // again would make the rate go as speed^2 and reintroduce the teleport
    // that integrating exists to prevent. With every audio uniform at zero
    // this drift and this tumble are the whole animation, and they are what
    // keeps a silent screen alive.
    vec3 drift = vec3(0.031, -0.047, 0.019) * uTime;
    mat3 volumeRot = rotY(NEB_TUMBLE_Y * uTime + NEB_MID_STEER * mid) * rotX(NEB_TUMBLE_X * uTime);

    // ---- the fingers, hoisted out of the march ----------------------------
    // One uniform branch. Every touch uniform is a literal zero on an
    // untouched frame, so this is false, nothing below runs, and the frame is
    // bit-identical to one from a build without touch in it.
    bool touching = !touchIdle();
    float aspect = uResolution.x / max(uResolution.y, 1.0);
    vec2 bridgeA = vec2(0.0);
    vec2 bridgeAxis = vec2(0.0);
    float bridgeLenSq = 0.0;
    float bridgeAmp = 0.0;
    float spinAngle = 0.0;
    if (touching) {
        // The corridor runs from the anchor along uTouchAxis, because that is
        // literally what those two uniforms are: TouchField tracks the anchor
        // to live point 0 and sets the axis to point1 - point0. Both decay
        // together on release - the axis with RELEASE_TAU, the strength with
        // it - so lifting a finger collapses the bridge back into the anchor's
        // own glow instead of switching it off. Reading uTouchGesture here
        // would have deleted that: it drops to 0 on the frame the last finger
        // leaves.
        bridgeA = touchAnchor();
        bridgeAxis = vec2(uTouchAxis.x * aspect, uTouchAxis.y);
        bridgeLenSq = dot(bridgeAxis, bridgeAxis);
        bridgeAmp = touchStrength();
        // uTouchSpin is a RATE, and it is used here as an ANGLE on purpose:
        // integrating it against uTime would run away and would jump whenever
        // the rate changed. As an angle the volume twists while the fingers
        // swirl and unwinds when they stop, which is what a hand on a fluid
        // actually does.
        spinAngle = uTouchSpin * NEB_SPIN_GAIN;
    }

    // ---- bound the march --------------------------------------------------
    // Outside the support sphere the envelope is exactly zero, so this is not
    // an approximation: it is the whole region that can possibly contribute.
    // The touch margin covers the one way a point outside it can acquire
    // density - being dragged in by touchWarp3.
    float support = radius * NEB_SUPPORT_K + NEB_TOUCH_MARGIN * touchStrength();
    float b = dot(ro, rd);
    float c = dot(ro, ro) - support * support;
    float disc = b * b - c;
    float sq = sqrt(max(disc, 0.0));
    float tNear = max(-b - sq, 0.0);
    float span = max((-b + sq) - tNear, 0.0);

    float volSteps = min(max(uSteps * NEB_STEP_SCALE, NEB_MIN_STEPS), float(NEB_MAX_STEPS));
    // A miss costs zero iterations rather than a branch around the loop.
    if (disc <= 0.0) volSteps = 0.0;
    // No upper clamp on the slab. The span is bounded by twice the support
    // radius by construction, so the worst case is 4.6 world units over the 45
    // steps the lowest Detail buys - a slab of 0.10, half a filament thick.
    // The march therefore always covers the volume it was handed, and clamping
    // ds would only make it stop short of the far side and draw a plane there.
    float ds = max(span / max(volSteps, 1.0), NEB_MIN_STEP);

    // Per-pixel start offset inside the first slab.
    //
    // Every ray otherwise samples the medium at the same depths and the
    // integral quantizes into concentric shells - the classic dark-gradient
    // banding, and on OLED it is the most visible artifact this style can
    // produce. Offsetting by a fraction of one slab turns that into
    // per-pixel noise, which the eye reads as grain. It is a STRATIFIED
    // sample, so the estimator stays unbiased and the cloud does not get
    // brighter or dimmer for being dithered.
    //
    // Hashed on the integer pixel and nothing else: a time-varying dither
    // would crawl, because there is no temporal filter downstream to resolve
    // it. hashCell over fract(sin(dot(...))) for the reason lib_sdf3 gives -
    // the sin form degenerates into bands at mediump, which is precisely the
    // artifact being fixed.
    float jitter = hashCell(ivec3(ivec2(gl_FragCoord.xy), 0));
    float t = tNear + jitter * ds;

    vec3 col = vec3(0.0);
    float trans = 1.0;

    for (int i = 0; i < NEB_MAX_STEPS; i++) {
        if (float(i) >= volSteps) break;
        if (trans < NEB_MIN_TRANSMITTANCE) break;

        vec3 pc = ro + rd * t;

        // The finger contracts space toward its own world ray, so the medium
        // GATHERS around it. Identity when idle, and no Lipschitz correction
        // is needed even though the warp is non-rigid: nothing here steps by a
        // distance estimate, the slab length comes from the bounding sphere,
        // so there is no estimate to overshoot. (A surface style doing this
        // would have to divide by touchWarpLipschitz().)
        vec3 pw = touchWarp3(pc);
        if (abs(spinAngle) > 1e-5) {
            // Vortex: a shear, not a rigid spin - the twist is strongest on
            // the view axis and relaxes outward, so three fingers wring the
            // volume instead of turning the whole picture.
            pw.xy = rot2(spinAngle * exp(-dot(pw.xy, pw.xy) * NEB_SPIN_TIGHT)) * pw.xy;
        }

        float rSq = dot(pw, pw);
        float env = nebEnvelope(rSq, radius);
        if (env <= 0.0) {
            // Exactly zero, not nearly - see NEB_ENV_CUTOFF. Skipping here
            // cannot leave a seam because there is nothing on the other side
            // of it. This is the cheap half of the march.
            t += ds;
            continue;
        }
        float r = sqrt(rSq);

        // Rotate into the volume's own frame before drifting and warping, so
        // the tumble turns the STRUCTURE and not the envelope (which is
        // spherical and would not notice).
        vec3 pn = volumeRot * pw;
        vec3 q = nebWarpCoord(pn + drift);

        // Octaves fall away with depth. A slab at the far side of the volume
        // covers several times the world width of one at the near side, so its
        // finest octaves are below the pixel and contribute aliasing rather
        // than detail - dropping them is cheaper AND cleaner. fbm3 normalizes
        // by its amplitude total, so the far octaves change the detail without
        // changing the brightness and no seam appears where the count steps.
        float lod = clamp((t - tNear) / max(span, 1e-3), 0.0, 1.0);
        int oct = NEB_MAX_OCTAVES - int(lod * float(NEB_MAX_OCTAVES - NEB_MIN_OCTAVES) + 0.5);
        float f = fbm3(q, oct);

        // ---- how much medium is here --------------------------------------
        float well = 0.0;
        float wake = 0.0;
        float bridge = 0.0;
        if (touching) {
            // Measured on the UNWARPED point: the warp has already moved the
            // medium toward the finger, and measuring the falloff in warped
            // space too would compound the two and drag the well off the
            // fingertip.
            //
            // These are distances to the anchor's world RAY, not to a point in
            // space - pc.xy is the ray's offset from the view axis at this
            // depth, and the anchor line runs parallel to it. So the well is a
            // column through the volume rather than a ball inside it, which is
            // what makes it stay under the finger at every depth instead of
            // only at the plane the finger nominally touches.
            //
            // All three only lower the threshold, and the threshold is
            // multiplied by an envelope that is zero outside the bounding
            // sphere, so none of them can put density where the march is not
            // looking.
            well = touchFalloff(pc.xy, NEB_TOUCH_RADIUS);
            wake = touchWake(pc.xy);
            if (bridgeLenSq > 1e-8) {
                float h = clamp(dot(pc.xy - bridgeA, bridgeAxis) / bridgeLenSq, 0.0, 1.0);
                vec2 bd = (pc.xy - (bridgeA + bridgeAxis * h)) / NEB_CORRIDOR_RADIUS;
                bridge = bridgeAmp * exp(-dot(bd, bd));
            }
        }

        // Uniform branch: at silence shockAmp is exactly 0 and the shell costs
        // the march nothing at all.
        float shock = 0.0;
        if (shockAmp > 0.0) {
            float sd = (r - shockR) / NEB_SHOCK_WIDTH;
            shock = shockAmp * exp(-sd * sd);
        }
        float thr = max(
            NEB_THRESHOLD
                - NEB_WELL_THRESHOLD * well
                - NEB_BRIDGE_THRESHOLD * bridge
                - NEB_SHOCK_THICKEN * shock,
            NEB_MIN_THRESHOLD
        );
        float dens = env * max(f - thr, 0.0) * NEB_DENSITY_GAIN;

        if (dens > 0.0) {
            // ---- light reaching this sample -------------------------------
            // Long range, closed form: the column of medium between here and
            // the core. Saturates once the sample is outside the cloud, which
            // is what stops the far skirts going pitch black.
            float column = radius * (1.0 - exp(-NEB_COLUMN_K * r / radius));
            float coreShadow = exp(-column * NEB_CORE_SHADOW * extRatio);

            // Short range: two taps straight down the radius. Because the
            // taps walk exactly toward the origin, the envelope at the tap is
            // just nebEnvelope((r - lt)^2) - no point to build, no length to
            // take. The NOISE coordinate is the one already computed, shifted
            // by the same step: re-running nebWarpCoord() there would double
            // the frame's cost for a correction smaller than the warp field's
            // own wavelength, which is many light-steps long.
            vec3 toCoreN = r > 1e-4 ? -pn / r : vec3(0.0, 0.0, 1.0);
            float ltrans = 1.0;
            float lt = NEB_LIGHT_STEP;
            for (int j = 0; j < NEB_LIGHT_TAPS; j++) {
                if (ltrans < NEB_LIGHT_CUTOFF) break;
                float lr = r - lt;
                float lenv = nebEnvelope(lr * lr, radius);
                float lf = fbm3(q + toCoreN * (lt * NEB_DENSITY_SCALE), NEB_SHADOW_OCTAVES);
                float ld = lenv * max(lf - thr, 0.0) * NEB_DENSITY_GAIN;
                ltrans *= exp(-ld * extinction * NEB_LIGHT_STEP);
                lt += NEB_LIGHT_STEP;
            }

            // Inverse-square from a soft core, floored so the centre is a
            // bright lamp rather than a singularity.
            float geom = 1.0 / (1.0 + NEB_CORE_FALLOFF * rSq);
            float lit = geom * coreShadow * ltrans;

            // The ridge tops of the field that already passed the mask: thin
            // bright veins along the spine of each strand. Squared, so the
            // light lands on a line and not on the region around it - which is
            // also why treble reads as detail here rather than as brightness.
            float vein = smoothstep(NEB_VEIN_THRESHOLD, NEB_VEIN_TOP, f);

            // pal() may sample the cyclic-palette atlas, and this is inside
            // non-uniform control flow, where GLSL ES leaves an implicit-LOD
            // texture fetch's derivatives undefined. It is safe here because
            // CyclicPalettes builds that atlas with GL_LINEAR and never
            // generates mipmaps, so level 0 is the only level there is to pick.
            // Hoisting the call above the branch would cost a palette lookup on
            // every empty sample instead, which is most of them.
            float shade = clamp(dens * NEB_HUE_DENS_NORM, 0.0, 1.0);
            vec3 tint = pal(
                NEB_HUE_BASE
                    + NEB_HUE_DENSITY * shade
                    + NEB_HUE_DEPTH * lod
                    + NEB_HUE_MID * mid
                    - NEB_HUE_CORE * geom
            );

            float emit = NEB_AMBIENT_EMIT
                + NEB_CORE_EMIT * lit
                + NEB_VEIN_EMIT * vein * vein * (NEB_VEIN_BASE + treble)
                + NEB_SHOCK_EMIT * shock
                // touchWake sums every slot and is unbounded above - five
                // fingers sum to five. Tone-mapped rather than clamped so the
                // fifth finger still adds something and nothing ever clips.
                + NEB_WAKE_EMIT * (1.0 - exp(-wake))
                + NEB_BRIDGE_EMIT * bridge;
            // Energy scales the sum BEFORE the knee, so a loud passage reaches
            // the ceiling sooner rather than moving it.
            emit *= glowGain;
            emit = emit / (1.0 + emit / NEB_EMIT_CEILING);

            // Emission first, with the transmittance the ray ARRIVED with:
            // this slab has not absorbed anything yet and must not shadow
            // itself.
            float absorbed = 1.0 - exp(-dens * extinction * ds);
            col += tint * (emit * trans * absorbed);
            trans *= 1.0 - absorbed;
        }

        t += ds;
    }

    // What is left of the background comes through whatever the cloud did not
    // absorb - the correct composite, and the reason a thin nebula sits in
    // space rather than on top of it.
    vec3 sky = pal(NEB_SKY_HUE + NEB_SKY_TILT * rd.y) * (NEB_SKY_LEVEL * glowGain);
    col += sky * trans;

    // Corner falloff, floored: on a wide aspect a fade to zero would black out
    // a third of the frame.
    col *= mix(NEB_VIGNETTE_FLOOR, 1.0, smoothstep(NEB_VIGNETTE_OUTER, NEB_VIGNETTE_INNER, length(uv)));

    fragColor = vec4(grade(col), 1.0);
}
