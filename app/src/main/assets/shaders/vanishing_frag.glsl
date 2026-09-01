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

// ===========================================================================
// VANISHING - the Droste effect, volumetric.
//
// An endless stack of concentric shells, each one exp(BAND) times the radius
// of the one inside it, falling outward past a camera that is forever dropping
// toward the centre. Every shell is the SAME shell: there is one prototype and
// a coordinate change, so the regress costs nothing to make deeper.
//
// ---- the mapping ----------------------------------------------------------
//
// The technique is log-spherical tiling. Forward-map a point to
// (log r, acos(z/r), atan(y,x)), tile the RADIAL coordinate, map back with
//
//     vec3 ilogspherical(vec3 p) {
//         float erho = exp(p.x), sintheta = sin(p.y);
//         return vec3(erho * sintheta * cos(p.z),
//                     erho * sintheta * sin(p.z),
//                     erho * cos(p.y));
//     }
//
// evaluate ONE prototype shell there, and multiply the returned distance by
// the radial scale factor exp(log r - log r_tiled). That correction is the
// whole trick: the prototype answers in its own units, and a ray stepping the
// uncorrected number overshoots by the tile's scale and walks straight through
// the surface, which shows up as holes that move when the camera does.
//
// The pair above is never actually called here, and the reason is worth
// writing down rather than discovering. The inverse map only needs the
// DIRECTION and the tiled radius, and the forward map's theta/phi exist only
// to be fed straight back into it - acos() then cos(), atan() then cos()/sin(),
// four transcendentals to reproduce a unit vector the point already is. So the
// round trip is fused: the direction is taken as p/|p| (rotated in the xy plane
// for the spiral, which is the one thing the round trip was for), and
// ilogspherical collapses to `dir * exp(fk)`. Same field, four fewer
// transcendentals per march step, and the step count is what this style spends
// its budget on.
//
// ---- the two artifacts, and what is done about them ------------------------
//
// POLAR PINCHING. phi is degenerate at theta = 0 and pi: every azimuth meets
// at the poles, so anything defined periodically in phi converges to a point
// there and the geometry visibly pinches. Two independent guards, because one
// of them is only cosmetic:
//
//   1. The pole is welded to +z and the camera sits on it, and the prototype's
//      six windows are cut along the axes - so BOTH poles are holes. The pinch
//      happens where there is no surface to pinch, and looking down it is the
//      infinite corridor this style is for. This is also why the two-finger
//      gesture below does not get to tilt the axis: dragging the pole into the
//      shell wall would put the artifact on screen in exchange for nothing.
//   2. The phi-varying relief is multiplied by sin(theta) - exactly the factor
//      the sphere's metric divides by when it converts d/dphi into a world
//      gradient. Without it |grad h| goes as 1/sin(theta) and is UNBOUNDED at
//      the poles, which is not a look, it is an overestimated distance and a
//      column of holes punched up the axis. With it the bound is a constant.
//      The spiral shear is well behaved at the poles for the same reason: its
//      contribution carries the same sin(theta) and dies there.
//
// SHELL-SEAM DISCONTINUITY. A tiled field only knows the contents of its own
// tile, so the shell in the next band is invisible to it and the seam between
// bands reads as a crack. The fix here is the explicit neighbour min, not a
// guide cone: for a family of shells at radii exp(fall + m*BAND), exp() is
// monotone, so the nearest shell in LINEAR radius is the floor or the ceiling
// of the log index and can never be anything else. Two candidates is therefore
// not an approximation of the union, it IS the union - for the price of one
// extra prototype evaluation, and with nothing parked in the middle of the
// composition to hide behind. (The windows make it a bound rather than an
// equality: where both nearest shells are open and a third is not, the min
// over two can overestimate. That is what STEP_SCALE is for.)
// ===========================================================================

// ---- the stack ------------------------------------------------------------

/**
 * Log-radius per shell. exp(0.9) = 2.46, so each shell is two and a half times
 * the one inside it: wide enough that a shell reads as its own object rather
 * than as a thickness in a gradient, tight enough that six of them fit down
 * the corridor before the sub-pixel floor takes over.
 *
 * Fixed, not a control. BAND_UP/BAND_DOWN below are compile-time exp(+-BAND)
 * and the whole neighbour min is built on them being constants; and the fall's
 * wrap (BAND_CYCLE) has to be a whole number of bands or the stack jumps.
 */
const float BAND = 0.9;
const float INV_BAND = 1.1111111;
const float BAND_UP = 2.4596031;
const float BAND_DOWN = 0.40656966;

/**
 * Shells before the colour repeats, and the period the fall wraps at.
 *
 * These are the same number on purpose. The GEOMETRY repeats every band, so
 * the fall could wrap at 1 - but the hue is per-shell, and a wrap that shifts
 * the shell index by one shifts every hue by one step and the whole frame
 * changes colour in a frame. Wrapping at six leaves hue = fract(index/6)
 * exactly where it was, so the wrap is invisible in both channels.
 *
 * Something has to wrap: the fall is what makes the camera descend, and an
 * unwrapped one drives exp() of a number that grows without bound until the
 * distance estimate underflows to zero and the march dies. Six bands keeps
 * every exp() argument inside +-11.
 */
const float BAND_CYCLE = 6.0;

/**
 * Bands per second of ambient fall, and the bass surge on top of it.
 *
 * uTime is ShaderScene's speed-INTEGRATED clock, so it is never multiplied by
 * uSpeed here - see the note in julia_frag: at t = elapsed * speed, nudging
 * the slider teleports by elapsed * delta-speed, which is exactly what
 * integrating exists to prevent.
 *
 * The bass term is an OFFSET, not a rate, for the same reason. Bass is
 * supposed to drive the fall SPEED, and the obvious spelling - uTime * (base +
 * bass) - has the same defect one order up: the moment bass moves, the whole
 * accumulated clock is rescaled and the stack jumps by minutes' worth of
 * travel. A bounded push forward integrates to the same thing where it
 * matters: d(fall)/dt picks up FALL_SURGE * d(bass)/dt, so an attack lurches
 * the camera inward and a decay eases it back, which is what a listener reads
 * as the drop pulling them in.
 *
 * 0.20 is sized against jitter, not against taste. uBass is an envelope but
 * not a slew-limited one (no uSlew* reaches the shared contract), and a
 * frame-to-frame wobble of 0.05 in uBass moves the stack by 0.20 * 0.75 * 0.05
 * = 0.0075 bands, against a shell wall that spans about 0.10 bands in log
 * radius. Under a tenth of the wall, so the wall does not shimmer.
 *
 * 0.18 bands/s also makes the fall cycle close at the uTime wrap:
 * ShaderScene.TIME_WRAP_SECONDS is 7100, and 0.18 * 7100 = 1278 = 213 whole
 * six-band cycles. Two hours is a long time to wait for a glitch and exactly
 * the kind nobody ever reproduces.
 */
const float FALL_RATE = 0.18;
const float FALL_SURGE = 0.20;

// ---- the prototype shell --------------------------------------------------

/** Wall half-thickness, in prototype units where the shell is the unit sphere. */
const float WALL = 0.045;

/**
 * Size of the solid octahedron subtracted from the shell to cut its windows,
 * and how far uEnergy opens it.
 *
 * On the unit sphere |x|+|y|+|z| runs from 1 on the axes to sqrt(3) on the
 * body diagonals, so subtracting |x|+|y|+|z| < s removes six caps around the
 * axes and leaves eight spherical triangles. The interesting number is
 * sqrt(2) = 1.4142: below it the eight triangles are joined by twelve
 * bridges into one lattice, above it the bridges part and the shell becomes
 * eight free caps. HOLE_BASE sits just ABOVE, because the lattice is a wall:
 * with the bridges intact the shell enclosing the viewer closes the frame
 * and the regress has one level. Free caps leave four open channels through
 * every shell, which is what puts five or six of them on screen at once.
 * uEnergy shrinks them further - density spent on how much of the regress
 * you can see, rather than on brightness.
 *
 * A closed shell would be the obvious prototype and is the wrong one: the
 * outermost one fills the frame and the Droste is a single sphere.
 */
const float HOLE_BASE = 1.47;
const float HOLE_SWING = 0.09;
/**
 * Rounds the window rims - and it is a MARCH parameter as much as a look.
 *
 * A hard max() of two exact fields never overestimates the distance to their
 * intersection; the smooth one rounds the join outward by up to k/4, and
 * that rounding is an overestimate in exactly the place a ray grazes the cut
 * edge of a shell. At 0.05 it was 0.0125 prototype units - a dozen hit
 * epsilons on the near shells - and it showed as a comb of fine hairs along
 * every rim, which reads as an aliasing bug and is actually the ray walking
 * through the wall. At 0.02 the rim resolves and its unlit inside edge draws
 * as the thin dark crescent it is.
 */
const float HOLE_BLEND = 0.02;

/**
 * Surface relief: latitude frequency, the amplitude in silence, and the
 * amplitude treble adds.
 *
 * Idle amplitude is not zero because a shell with no relief is a plastic
 * sphere; 0.009 against a 0.045 wall is a sheen. At full treble 0.024 is half
 * the wall thickness, which is as far as it can go before the crests start
 * cutting through the trough side.
 *
 * The pattern is four-fold in azimuth to agree with the four equatorial
 * windows, so the relief runs into the rims instead of across them.
 */
const float RELIEF_LAT = 4.0;
const float RELIEF_IDLE = 0.009;
const float RELIEF_TREBLE = 0.024;
/**
 * Bound on |grad h| per unit of amplitude, for the Lipschitz divisor.
 *
 * h = amp * sin(theta) * sin(4 phi) * sin(RELIEF_LAT * cos(theta) + phase) is
 * a function of direction only, so on a sphere of radius R its gradient is
 * (1/R) * sqrt(h_theta^2 + (h_phi/sin theta)^2). The sin(theta) factor cancels
 * the metric's 1/sin(theta) in the second term, leaving 4; the first is
 * bounded by 1 + RELIEF_LAT. R is smallest at BAND_DOWN, so
 * sqrt(25 + 16) / 0.40657 = 15.75.
 *
 * It is written out because the divisor is what keeps the displaced field
 * marchable, and changing RELIEF_LAT changes it.
 */
const float RELIEF_GRAD = 15.75;

/**
 * Drift rates, all of the form 2*pi*m/7100 so their sines land back where they
 * started at the uTime wrap. RELIEF_PHASE_W is m = 282 (0.25 rad/s, a slow
 * shimmer), SPIRAL_W is m = 42 (a 169 s breath), SPIN_W is m = 68 (3.4 deg/s,
 * enough that the lattice is never twice in the same place).
 */
const float RELIEF_PHASE_W = 0.2495575;
const float SPIRAL_W = 0.03716814;
const float SPIN_W = 0.06017699;

/**
 * The spiral: azimuth advances by SPIRAL radians per e-fold of radius, which
 * turns the concentric stack into a helicoid and is what makes a Droste read
 * as falling rather than as a target.
 *
 * It is also the only source of variety BETWEEN shells. Every shell is the
 * same prototype, so without the shear the whole stack lines up and the
 * corridor looks printed; with it, two shells at the same screen direction are
 * an e-fold apart in radius and so SPIRAL * BAND apart in azimuth, and their
 * windows and relief no longer register.
 *
 * SPIRAL_MAX is a cost ceiling, not a taste one. The shear's Jacobian norm is
 * 0.5*(|A| + sqrt(A*A + 4)), which every distance estimate is divided by; at
 * 0.5 that is 1.28, at 1.5 it would be 1.90 and half the march budget.
 */
const float SPIRAL_BASE = 0.20;
const float SPIRAL_DRIFT = 0.12;
const float SPIRAL_MID = 0.18;
const float SPIRAL_MAX = 0.5;

// ---- camera and march -----------------------------------------------------

/**
 * Camera radius. One, so that log(radius) is zero and every depth in this file
 * reads directly as e-folds inside or outside the viewer.
 *
 * The camera sits ON the polar axis, which is the one place a shell is open,
 * so it falls through the eye of each shell in turn rather than through a
 * wall. Nothing keeps it away from a shell surface because nothing has to.
 */
const float CAM_DIST = 1.0;
/**
 * Half-height at unit depth: atan(0.82) = 39 deg, so a 78 deg vertical view.
 *
 * Wide on purpose, and the width is doing structural work rather than
 * drama. The shell one band inside the viewer subtends about 47 degrees, so
 * at a normal lens it IS the frame and the regress has nowhere to be read
 * against; opening up to 78 leaves the shells enclosing the viewer in shot,
 * which is what tells the eye it is inside the stack rather than looking at
 * a rosette.
 */
const float FOV = 0.82;
/** Past this the shells are bigger than the fog can carry anyway. */
const float FAR = 22.0;
/**
 * Fraction of the estimate actually stepped.
 *
 * Under one because two things here are bounds rather than distances: the
 * smooth window subtraction rounds outward near a rim (see HOLE_BLEND), and
 * the two-candidate min is exact for the shell family but only a bound once
 * the windows are cut, since a third shell can be nearest through a hole.
 * 0.72 is where the rims stopped combing once HOLE_BLEND came down to 0.02;
 * with the old 0.05 blend even 0.62 still showed it, which is the tell that
 * the blend was the fault and the step scale was paying for it.
 */
const float STEP_SCALE = 0.72;
/**
 * Hit epsilon: one pixel footprint plus a floor.
 *
 * Proportional to t rather than constant, and that is doing real work here.
 * The stack is self-similar all the way down, so a ray aimed near the centre
 * meets shells whose spacing shrinks geometrically and would grind against the
 * step budget forever. A footprint epsilon terminates it exactly where the
 * regress stops being resolvable - about fourteen steps in - and what it stops
 * on is handed to the core glow rather than shaded.
 */
const float EPS_SLOPE = 0.0026;
const float EPS_FLOOR = 5e-4;
/**
 * Floor on |p| before log(). The corridor runs down the axis, so a ray really
 * does sample the origin and log(0) is -inf, which turns the band index into
 * NaN and the march into a hang. Inside this ball the direction shrinks and
 * the estimate collapses to a tiny positive number, so the ray terminates -
 * which is what should happen a ten-thousandth of a shell from the centre.
 */
const float R_MIN = 1e-4;
/** Compile-time bound. The runtime budget is uSteps (MarchBudget.forDetail, 64..128). */
#define MAX_STEPS 128

// ---- light ----------------------------------------------------------------

/**
 * Aerial perspective, in the three currencies this geometry has.
 *
 * FOG_T is per world unit travelled and is nearly idle inside the corridor,
 * where the whole regress happens within two units of the eye. The depth cue
 * that matters here is e-folds of RADIUS - how many times the world has
 * halved - so it gets its own two rates: FOG_K inward, which is what makes
 * the regress read as endless instead of as five rings, and FOG_OUT for the
 * shells enclosing the viewer, which are close, huge and would otherwise be
 * the brightest thing in frame purely for being nearby.
 *
 * FOG_K is low (0.72 transmission per band) because it is spent on depth
 * COUNT: at 0.62 the fourth shell in was already gone and the corridor
 * bottomed out just as the eye started counting.
 */
const float FOG_T = 0.030;
const float FOG_K = 0.36;
const float FOG_OUT = 0.50;
/**
 * e-folds inside the viewer at which the regress is handed wholly to the core.
 *
 * Set to where the march actually stops, not to taste: the footprint epsilon
 * terminates a corridor ray at about eps/0.35 in radius, which at t ~ 1 is
 * 0.009, or 4.7 e-folds down. Past that the hit is whichever shell the ray
 * grazed and its normal is quantization noise, so that is exactly where the
 * geometry has to stop being shaded. CORE_RAMP is how long the handover
 * takes - short, so the fog above keeps doing the work until the last band.
 */
const float CORE_DEPTH = 4.7;
const float CORE_RAMP = 1.2;
/** Angular half-width (as a tangent) of the light at the vanishing point. */
const float CORE_ANG = 0.16;
/**
 * Brightness of the tight lobe. Its idle floor is high (0.85 of this before
 * uEnergy adds anything) because the vanishing point has to read as a LIGHT
 * in silence - it is the one thing in frame that says the regress continues
 * past where the march gave up, and a dim one just looks like the corridor
 * ends in a smudge.
 */
const float CORE_GAIN = 0.95;
/**
 * The wide halo around it, and it is deliberately NOT audio-driven.
 *
 * This is the one term that covers most of the frame, so it is the one that
 * would show up in the flash budget. The tight core breathes with uEnergy
 * because it is a few percent of the screen; the halo is held constant so no
 * large-area luminance step can ever be scheduled by the music.
 */
const float HALO_GAIN = 0.20;
/** Floor tint, so an untouched silent frame is a dark room and not a fault. */
const float BACKDROP = 0.030;

/**
 * The beat shell: born FLARE_BIRTH e-folds inside the viewer, travelling
 * FLARE_SPAN e-folds outward over one beat.
 *
 * Position rides uBeatPhase, which RESETS to 0 on a heard transient, so the
 * front restarts deep in the corridor on every hit and free-runs outward
 * between them; amplitude rides the SQUARED beat envelope, so it is silent in
 * silence rather than pulsing through it at the last known tempo.
 *
 * FLARE_TAPER is a photosensitivity term, not an aesthetic one. The front
 * covers the most screen area exactly when it reaches the shell enclosing the
 * viewer, so it is faded out from 0.8 e-folds before that: the flare peaks
 * mid-corridor, on a shell covering well under a fifth of the frame, and has
 * mostly gone by the time it is all around you. FLARE_GAIN is then set so even
 * that peak is a little over half a palette step on lit material.
 */
const float FLARE_BIRTH = -2.6;
const float FLARE_SPAN = 3.6;
const float FLARE_WIDTH = 0.30;
const float FLARE_GAIN = 0.55;
const float FLARE_TAPER = 1.6;

/**
 * Angular frequency of the cosmetic surface grain.
 *
 * Painted at the hit point, not displaced into the field, and that is the
 * whole reason it can be this fine. A displacement at frequency f multiplies
 * the Lipschitz divisor by roughly 1 + amp*f and every pixel in the frame
 * pays for it in march steps, whether it can see the detail or not; a
 * shading term costs three sines on the pixels that actually landed on a
 * wall. At this scale the eye cannot tell the two apart, and the geometric
 * relief above is still there to catch the light where it can be seen.
 *
 * Defined on the DIRECTION, so every shell carries the same pattern at the
 * same angular size about the centre - which is what self-similar means -
 * and the inner ones therefore show it finer from where the eye is.
 */
const float GRAIN = 23.0;

/** Palette origin and how far the six shell hues walk from it. */
const float HUE_BASE = 0.06;
const float HUE_SPAN = 0.65;

// ---- touch ----------------------------------------------------------------

/**
 * How far toward the finger the vanishing point actually goes.
 *
 * Not 1.0. The last fifteen percent is withheld so the point trails the
 * fingertip instead of being welded to it - a vanishing point that tracks
 * exactly reads as a cursor, and this one is supposed to read as the whole
 * world swinging round to aim somewhere. TouchField already chases the raw
 * pointer with a 0.06 s time constant; this is the second, gentler stage the
 * brief asks for, and it is applied through smoothstep(strength) so the re-aim
 * eases in on contact and unwinds over the release rather than cutting.
 */
const float VP_GAIN = 0.85;
/**
 * Bands of extra fall under the finger, and the radius of the dimple.
 *
 * This is a function of the SCREEN coordinate alone, so it is constant along
 * any one ray - which means each ray still marches an exact, uniformly scaled
 * copy of the stack and the Lipschitz divisor does not have to pay for it.
 * A pull that varied along the ray would be a genuine space warp and would
 * cost steps everywhere, touched or not.
 */
const float TOUCH_PULL = 0.85;
const float TOUCH_PULL_R = 0.45;
/**
 * Two fingers: separation sets the shell twist. Read straight off
 * |uTouchAxis| with no neutral point, because TouchField decays the axis
 * toward ZERO on release (not toward some resting separation) - so the twist
 * unwinds over RELEASE_TAU_SECONDS instead of snapping to whatever a neutral
 * offset would imply.
 *
 * Three or more: uTouchSpin turns the whole stack about the pole. Also read as
 * an ANGLE rather than integrated as the rate it nominally is, for the reason
 * the fall gives at length; TouchField slew-limits it with a 0.25 s constant
 * and decays it to zero below three fingers, so holding a swirl holds the
 * stack round and letting go lets it settle.
 */
const float TWIST_GAIN = 0.30;
const float SPIN_GAIN = 0.12;
const float SPIN_MAX = 0.9;
/** Light on the glass where the fingers are. touchWake() is unbounded above; five fingers are clamped to about two. */
const float WAKE_GAIN = 0.15;

// ---- per-pixel state ------------------------------------------------------
//
// Uniform along a ray, so they are computed once in main() and read by map()
// rather than threaded through it and through the normal taps. gShell and
// gReliefN go the other way - map() writes them, and the caller must copy them
// out BEFORE calling normalAt(), which runs map() four more times.

float gFall;
float gSpiral;
float gSpin;
float gHole;
float gReliefAmp;
float gReliefPhase;
float gLip;
float gShell;
/** The relief PATTERN, -1..1 and amplitude-free, so shading can read it without a divide. */
float gReliefN;

/**
 * One shell, in prototype units where its mid-wall is the unit sphere.
 *
 * qr is passed in because the caller already knows |q| exactly - it built q as
 * a unit direction times a radius - so sdSphere's length() would be recomputing
 * something in hand. `qr - 1.0` IS sdSphere(q, 1.0); opOnion turns the ball
 * into the wall, and smax against the negated octahedron cuts the windows.
 */
float shellProto(vec3 q, float qr, float relief) {
    float wall = opOnion(qr - 1.0, WALL);
    return smax(wall, -sdOctahedron(q, gHole), HOLE_BLEND) - relief;
}

/**
 * The stack. See the header for the mapping and for why there are exactly two
 * candidates.
 */
float map(vec3 p) {
    float r = max(length(p), R_MIN);
    float k = log(r);

    // Direction, without the acos()/atan() the forward map would spend. st and
    // ct are sin/cos of the polar angle straight off the components; the xy
    // part is rotated by rot2 - the library's spelling, so this style's idea of
    // a positive turn is view()'s idea of one - by the spiral shear plus the
    // stack's own slow spin. The max() guards the axis, where p.xy is EXACTLY
    // zero (the corridor runs down it, so this is a pixel that happens, not a
    // limit) and the division would be 0/0. st is zero there too, so whichever
    // azimuth the guard lands on is multiplied straight back out.
    float lxy = length(p.xy);
    float ct = p.z / r;
    float st = lxy / r;
    vec2 cs = rot2(gSpiral * k + gSpin) * (p.xy / max(lxy, 1e-20));
    vec3 dir = vec3(st * cs, ct);

    // Tile the radial coordinate. u is in units of bands so that the fall's
    // wrap shifts floor(u) by exactly BAND_CYCLE and the shell hue survives it;
    // doing this in log-radius and dividing later would leave that to rounding.
    float u = k * INV_BAND - gFall;
    float n = floor(u);
    float fk = (u - n) * BAND;
    float qr = exp(fk);
    vec3 q0 = dir * qr;

    // sin(4*phi) by the double-angle identity on the direction we already
    // have, rather than a fourth transcendental. The sin(theta) factor is the
    // pole guard - see the header.
    float s2 = 2.0 * cs.x * cs.y;
    float c2 = cs.x * cs.x - cs.y * cs.y;
    gReliefN = st * (2.0 * s2 * c2) * sin(RELIEF_LAT * ct + gReliefPhase);
    float relief = gReliefAmp * gReliefN;

    // exp(k - fk) without a second exp(): it is r/qr by construction. The two
    // candidates share one relief value and each scales it by its own factor,
    // so the outer shell wears the same bumps BAND_UP times larger - which is
    // what self-similar means, and what stops the pattern from crawling in
    // scale as a shell falls outward.
    float sc = r / qr;
    float dIn = shellProto(q0, qr, relief) * sc;
    float dOut = shellProto(q0 * BAND_DOWN, qr * BAND_DOWN, relief) * sc * BAND_UP;

    gShell = dOut < dIn ? n + 1.0 : n;
    return min(dIn, dOut) / gLip;
}

/**
 * Tetrahedron 4-tap against map(), the idiom lib_sdf3 spells out at the top of
 * the file - four evaluations instead of a central difference's six, and
 * symmetric, so a flat wall reads flat.
 */
vec3 normalAt(vec3 p, float e) {
    vec2 t = vec2(1.0, -1.0);
    return normalize(
        t.xyy * map(p + t.xyy * e) + t.yyx * map(p + t.yyx * e) +
            t.yxy * map(p + t.yxy * e) + t.xxx * map(p + t.xxx * e)
    );
}

void main() {
    // view() first, so Zoom, Rotation, Drift, Kaleido, Tile, Pixelate, Shake,
    // Twist, Warp, Ripple, Sway, Morph, Mirror and the beat pulse all mean here
    // what they mean everywhere else.
    vec2 uv = view();

    // ---- the fingers ------------------------------------------------------
    //
    // The anchor IS the vanishing point: subtracting it from the screen
    // coordinate is a lens shift, so the ray under the finger is the one that
    // runs down the polar axis through the centre of the map, and the entire
    // stack re-aims at wherever the finger is.
    //
    // It is subtracted in view()'s OUTPUT space, which is the space the ray is
    // built in. Under default Customize settings that puts the vanishing point
    // exactly under the finger. With Zoom or Rotation cranked it lands where
    // that transform sends it - the point stays where it belongs inside the
    // picture, and the picture is what moved. Chasing the finger back through
    // the inverse of a stack containing a kaleidoscope fold and a tiling wrap
    // is not a function with one answer, and lib_touch says as much.
    //
    // Everything below is exactly zero when nothing has been touched, so an
    // untouched frame is bit-identical to one from a style that never read
    // these uniforms: touchAnchor()/touchStrength()/touchFalloff()/touchWake()
    // all return literal 0 on the three-way idle test, and the axis and spin
    // reads are gated on the same test.
    float grip = touchStrength();
    vec2 vp = touchAnchor() * (VP_GAIN * smoothstep(0.0, 1.0, grip));
    float pull = TOUCH_PULL * touchFalloff(uv, TOUCH_PULL_R);
    float twist = touchIdle() ? 0.0 : TWIST_GAIN * length(uTouchAxis);
    float spin = touchIdle() ? 0.0 : clamp(SPIN_GAIN * uTouchSpin, -SPIN_MAX, SPIN_MAX);

    // ---- the music --------------------------------------------------------
    //
    // uBass..uEnergy are 0..1.5 auto-gained, so 0.75 puts a loud passage near
    // 1 and leaves headroom rather than clipping the middle of the range.
    float bassAmt = clamp(uBass * 0.75, 0.0, 1.0);
    float midAmt = clamp(uMid * 0.75, 0.0, 1.0);
    float trebAmt = clamp(uTreble * 0.75, 0.0, 1.0);
    float energyAmt = clamp(uEnergy * 0.7, 0.0, 1.0);

    // The pull is added OUTSIDE the wrap: mod()ing it would fold the dimple
    // and put a seam through the middle of it. The wrap itself is invisible
    // because the stack and the hue are both periodic at BAND_CYCLE.
    gFall = mod(uTime * FALL_RATE + FALL_SURGE * bassAmt, BAND_CYCLE) + pull;
    // Mids steer: they set how hard the stack spirals, which is the direction
    // the corridor screws away in.
    gSpiral = clamp(
        SPIRAL_BASE + SPIRAL_DRIFT * sin(uTime * SPIRAL_W) + SPIRAL_MID * midAmt + twist,
        -SPIRAL_MAX,
        SPIRAL_MAX
    );
    gSpin = uTime * SPIN_W + spin;
    gHole = HOLE_BASE + HOLE_SWING * energyAmt;
    gReliefAmp = RELIEF_IDLE + RELIEF_TREBLE * trebAmt;
    gReliefPhase = uTime * RELIEF_PHASE_W;

    // Worst-case Jacobian norm of everything between world space and the
    // prototype, which map() divides every estimate by. Two non-rigid stages:
    // the spiral shear (a unit shear in the orthonormal (r dk, r sin(theta)
    // dphi) frame, norm 0.5*(|A| + sqrt(A^2+4))) and the relief displacement
    // (1 + |grad h|). Both collapse to exactly 1 at zero spiral and zero
    // relief, so the cost of the deforms is paid only by the frames that use
    // them - a quiet frame marches at full speed.
    float as = abs(gSpiral);
    gLip = 0.5 * (as + sqrt(as * as + 4.0)) * (1.0 + gReliefAmp * RELIEF_GRAD);

    // ---- march ------------------------------------------------------------
    vec2 w = uv - vp;
    vec3 ro = vec3(0.0, 0.0, -CAM_DIST);
    vec3 rd = normalize(vec3(w * FOV, 1.0));

    float t = 0.05;
    float hitT = -1.0;
    float hitShell = 0.0;
    float hitRelief = 0.0;
    for (int i = 0; i < MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        float eps = EPS_SLOPE * t + EPS_FLOOR;
        if (d < eps) {
            hitT = t;
            // Copied out here because normalAt() runs map() four more times.
            hitShell = gShell;
            hitRelief = gReliefN;
            break;
        }
        // Bounded above by what is left of the ray and below by the epsilon: a
        // step finer than the hit threshold cannot resolve anything and would
        // spend the rest of the budget standing still.
        t += max(min(d * STEP_SCALE, FAR - t), eps);
        if (t > FAR) break;
    }

    // ---- the light at the end ---------------------------------------------
    //
    // Built on w, the lens-shifted screen offset, so it is the projection of
    // the map's centre and travels with the finger for free. Two lobes: a tight
    // one that is the vanishing point itself, and a wide constant halo that
    // keeps a silent frame from being a black rectangle.
    float ga = length(w) * FOV / CORE_ANG;
    float tight = exp(-ga * ga);
    float halo = exp(-ga * ga * 0.05);
    vec3 core = pal(HUE_BASE + 0.42 + 0.10 * midAmt) *
        (CORE_GAIN * tight * (0.85 + 0.95 * energyAmt) + HALO_GAIN * halo);
    vec3 backdrop = pal(HUE_BASE + 0.62) * BACKDROP + core;

    vec3 col = backdrop;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(EPS_SLOPE * hitT, 8e-4));
        float kHit = log(max(length(p), R_MIN));

        // One hue per shell, welded to the shell INDEX, so a shell keeps its
        // colour the whole way out and the corridor reads as a cascade rather
        // than as one object seen repeatedly; fract() of index/BAND_CYCLE is
        // what makes the fall's wrap a no-op. The direction term on the end
        // stops the eight caps of one shell from being eight identical chips -
        // without it each level of the regress is one flat colour and the frame
        // reads as a rosette rather than as a room.
        vec3 sd = p / max(length(p), R_MIN);
        float hue = HUE_BASE + fract(hitShell / BAND_CYCLE) * HUE_SPAN +
            0.08 * midAmt + 0.05 * hitRelief + 0.06 * dot(sd, vec3(0.42, 0.74, 0.21));
        vec3 body = pal(hue);

        // Key plus headlight. There is no sun inside a nest of shells, so most
        // of the modelling comes from the camera-facing term; the key exists to
        // break the symmetry, and because the stack turns through it the same
        // wall is lit differently on its way past.
        vec3 key = normalize(vec3(0.42, 0.70, -0.58));
        float dif = clamp(dot(n, key), 0.0, 1.0);
        float head = clamp(dot(n, -rd), 0.0, 1.0);
        // Treble sharpens edges and sparkles: it tightens the rim exponent and
        // the specular lobe rather than adding brightness, so a bright mix
        // reads as crisper metal instead of as a raised level.
        float rim = pow(1.0 - head, 2.2 + 3.0 * trebAmt);
        float spec = pow(clamp(dot(n, normalize(key - rd)), 0.0, 1.0), 22.0 + 70.0 * trebAmt);

        // The grain fades with depth rather than aliasing into the corridor:
        // past three e-folds down it is finer than a pixel and would boil.
        float grain = sin(GRAIN * sd.x + gSpin) * sin(GRAIN * sd.y - gSpin) *
            sin(GRAIN * sd.z + 3.0 * kHit);
        grain *= smoothstep(3.0, 1.2, -kHit) * (0.35 + 0.45 * trebAmt);
        float emboss = 1.0 + 0.45 * hitRelief + 0.30 * grain;

        col = body * ((0.09 + 0.52 * dif + 0.40 * head) * emboss);
        col += pal(hue + 0.12) * (rim * (0.20 + 0.50 * trebAmt));
        col += vec3(1.0) * (spec * (0.18 + 0.40 * trebAmt) * (0.7 + 0.5 * grain));

        // The beat's shell, sweeping outward through the stack. Position off
        // uBeatPhase, amplitude off the SQUARED envelope - the house pairing,
        // so the front is silent between hits instead of pulsing through the
        // quiet parts at the last detected tempo. This is the only thing in
        // the style the beat is allowed to move: the fall, the spiral, the
        // windows and the relief are all continuous quantities and are
        // steered by envelopes, never by an impulse.
        float beatEnv = clamp(uBeat, 0.0, 1.0);
        float dk = (kHit - (FLARE_BIRTH + uBeatPhase * FLARE_SPAN)) / FLARE_WIDTH;
        float flare = beatEnv * beatEnv * exp(-dk * dk) *
            exp(-max(0.0, kHit + 0.8) * FLARE_TAPER);
        col += pal(hue + 0.30) * (FLARE_GAIN * flare);

        col *= 0.80 + 0.55 * energyAmt;

        // Aerial perspective. The inward term is what makes the regress read
        // as infinite; the outward one pushes the shell enclosing the viewer
        // back into being a frame rather than a wall.
        float fade = 1.0 - exp(
            -(hitT * FOG_T + max(0.0, -kHit) * FOG_K + max(0.0, kHit) * FOG_OUT)
        );
        // Below the sub-pixel floor the march stops on whichever shell it
        // grazed and the 4-tap normal there is quantization noise. Rather than
        // shade noise, hand it wholly to the core: past CORE_DEPTH e-folds the
        // regress has stopped being geometry and become light.
        fade = max(fade, smoothstep(CORE_DEPTH - CORE_RAMP, CORE_DEPTH, -kHit));
        col = mix(col, backdrop, fade);
    }

    // The wake sits on top of everything because it is light on the glass, not
    // in the room. touchWake() sums all five slots and is unbounded above, so
    // it is clamped before it can multiply a palette colour past the grade.
    col += pal(HUE_BASE + 0.20) * (WAKE_GAIN * min(touchWake(uv), 2.5));

    // Vignette on the RAW screen radius, not on uv: this is a property of the
    // glass, and reading the drifted/zoomed domain would slide it off frame.
    vec2 sv = (vUv - 0.5) * vec2(uResolution.x / max(uResolution.y, 1.0), 1.0) * 2.0;
    col *= mix(1.0, smoothstep(2.0, 0.30, length(sv)), 0.5);

    fragColor = vec4(grade(col), 1.0);
}
