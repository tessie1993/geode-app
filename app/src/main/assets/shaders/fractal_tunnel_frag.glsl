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
//  FRACTAL TUNNEL - falling down a bore through a foam of nested spheres.
//
//  WHAT IS DRAWN. One prototype with no shape of its own - the plane y = 0 -
//  seen through a chain of alternating TRANSLATIONS (domain repetition into
//  a unit cell) and SPHERE INVERSIONS about the cell's centre. The group
//  those two generate has a limit set of spheres packed inside spheres at
//  every scale: the three-dimensional Apollonian construction. Nothing in
//  this file has the shape of a sphere; every sphere on screen is what the
//  inversions do to that plane. The corridor the camera falls down is bored
//  through the foam with one cylinder, so the walls are the packing cut
//  open - rings of circles at every size, with the cavities behind them
//  showing through - and a wedge fold about the bore's axis gives the
//  cross-section a kaleidoscope's mirrored symmetry.
//
//  THE CAMERA FALLS. Its position is the scene clock times a speed, along a
//  tunnel axis that bends on two slow lobes; the frame looks a little way
//  down the path, so it banks into the bends. The whole world is periodic
//  in z, and the flight speed is chosen so the clock's wrap lands exactly on
//  a world period (see FT_FLY), so the tunnel never teleports.
//
//  THE FRACTALS MOVE, four ways, all continuous: the inversion radius
//  breathes (spheres grow, split and merge - the morph), a rotation between
//  the inversions turns the packing inside itself, the packing slides
//  sideways across the bore, and the bore twists about its axis so the
//  chains of beads spiral away into the distance. Bass, mids and the beat
//  modulate those; they do not create them, so silence still shows a tunnel
//  being fallen down rather than a frozen fractal.
//
//  WHY THE ESTIMATE IS SAFE TO MARCH. The repetition, the rotation between
//  the inversions, the wedge fold and the spin are isometries; each
//  inversion is conformal with the scalar local factor s / r2, floored so it
//  can never reach infinity. The estimate is the plane distance in the final
//  frame divided by the product of those factors, times FT_DE_FUDGE because
//  a conformal pullback is only first order, and the march then takes a
//  further fraction of it (FT_STEP) for the twist and the bend of the
//  tunnel frame, which are not isometries. The shells are given a real
//  thickness (FT_SHELL) so the field is smooth at the surface the ray
//  lands on instead of creased there, which is what keeps the normal clean.
//
//  Technique reference: the translate/invert chain is the construction of
//  Inigo Quilez's "Apollonian" (Shadertoy, 2013). The code here is written
//  from the construction, not copied; the floor, the rotation between the
//  folds, the shell thickness, the bore and the tunnel frame are this
//  style's own.
// ===========================================================================

#define FT_TAU 6.2831853

/** Compile-time ceiling on the march: MarchBudget.MAX_STEPS. uSteps is the runtime budget. */
#define FT_MAX_STEPS 128

/** Compile-time ceiling on the inversion chain; the runtime count gIters breaks below it. */
#define FT_MAX_ITERS 10

// ---- clocks and periods ---------------------------------------------------

/**
 * ShaderScene.TIME_WRAP_SECONDS: uTime restarts from zero every this many
 * seconds. Every rate in this style is built from it so the restart is
 * invisible - a camera that is translating at speed would otherwise teleport
 * the whole tunnel once every two hours, which on a wallpaper is a cut.
 */
const float FT_TIME_WRAP = 7100.0;

/** A rate that completes exactly k cycles per uTime wrap, so sin(uTime * FT_RATE(k)) is continuous across it. */
#define FT_RATE(k) (FT_TAU * (k) / FT_TIME_WRAP)

/** Foam units per world unit: the unit cell of the packing is 2 foam units wide, so 0.5 world units here. */
const float FT_FOAM_FREQ = 2.0;

/** Period of the foam along the bore, in world units. */
const float FT_FOAM_PERIOD = 2.0 / FT_FOAM_FREQ;

/** Foam periods per world period. */
const float FT_CELLS = 42.0;

/** The world repeats along z every this many units; every z-dependent term below is periodic in it. */
const float FT_PERIOD = FT_CELLS * FT_FOAM_PERIOD;

/**
 * Flight speed, world units per second of scene clock: 270 world periods per
 * uTime wrap, so the wrap lands on a period boundary and the world the camera
 * sees at the restart is the world it was already looking at.
 */
const float FT_FLY = 270.0 * FT_PERIOD / FT_TIME_WRAP;

// ---- the tunnel ------------------------------------------------------------

/** How far the tunnel axis swings sideways, and the lobes per world period on each axis (integers, so the bend is periodic). */
const float FT_BEND = 0.75;
const float FT_BEND_LOBES_X = 3.0;
const float FT_BEND_LOBES_Y = 2.0;

/** Turns of the bore about its own axis per world period (an integer, so the twist is periodic for any wedge count). */
const float FT_TWIST_TURNS = 2.0;
const float FT_TWIST = FT_TWIST_TURNS * FT_TAU / FT_PERIOD;

/** Radius of the corridor bored through the foam, and how far bass and a pinch open it. */
const float FT_CORRIDOR = 0.85;
const float FT_CORRIDOR_BASS = 0.05;
const float FT_CORRIDOR_PINCH = 0.6;

/** Deeper inside the bore than this, nothing can be hit and the foam is never evaluated. */
const float FT_BORE_MARGIN = 0.02;

/** Wedges the cross-section is folded into when the user's Kaleidoscope is off. */
const float FT_WEDGES = 6.0;

// ---- the foam --------------------------------------------------------------

/**
 * The inversion radius squared, in foam units: the resting value, the slow
 * breath, and what bass and the beat add. This is the morph. Near 1 the
 * packing is dense and the cavities small; toward 1.5 the spheres are few
 * and large. The swing is slow (FT_RATE(19), a cycle every six minutes)
 * because every sphere on screen moves when it moves, and bass is held
 * small for the same reason: a per-frame band envelope on a whole-frame
 * parameter is flicker at any gain above this.
 */
const float FT_INV_BASE = 1.18;
const float FT_INV_SWING = 0.22;
const float FT_INV_BASS = 0.08;
const float FT_INV_BEAT = 0.03;

/**
 * Floor on r2 inside the inversion. Below it the map is a plain uniform
 * scale, whose second derivative is zero - so the region where the conformal
 * pullback would be least accurate is exactly the region where it becomes
 * exact. Without it the reciprocal returns inf at the pole and one NaN
 * distance is a ray that never terminates.
 */
const float FT_INV_FLOOR = 0.02;

/** Cap on the accumulated conformal scale; a guard, not a working limit. */
const float FT_SCALE_CAP = 1.0e30;

/** The rotation between the inversions: its swing, and how far mids steer it. */
const float FT_FOLD_SWING = 0.30;
const float FT_FOLD_MID = 0.08;

/** How far the packing slides across the bore, in foam units. */
const float FT_SLIDE = 0.40;

/** The spin of the whole cross-section: mids, the beat's lurch, and three-plus fingers. */
const float FT_SPIN_MID = 0.12;
const float FT_SPIN_BEAT = 0.06;
const float FT_TOUCH_SPIN = 0.05;

/** Inversions at the lowest Detail; Detail buys two more and the beat one more, all under FT_MAX_ITERS. */
const float FT_ITERS_BASE = 6.0;

/** Trap value reported where no foam was evaluated: a defined "far", not a huge number. */
const float FT_TRAP_FAR = 4.0;

// ---- march -----------------------------------------------------------------

/**
 * Fraction of the pulled-back plane distance the estimate reports. A
 * conformal pullback is first order, and the shells it describes curve away
 * from the tangent plane the estimate measures to; 0.4 is the margin that
 * keeps a ray from stepping through a sphere it is skimming.
 */
const float FT_DE_FUDGE = 0.4;

/** Fraction of the estimate a step takes: the twist and the bend are not isometries. */
const float FT_STEP = 0.8;

/**
 * Half-thickness of every shell, in world units. Small spheres become solid
 * beads, large ones stay thin shells, and the field is a smooth signed
 * distance at the surface the ray stops on rather than a crease.
 */
const float FT_SHELL = 0.012;

/** Surface epsilon as a multiple of the pixel footprint, and its floor for the near field. */
const float FT_EPS_PIXELS = 1.2;
const float FT_EPS_FLOOR = 4.0e-4;

/** Nothing past this is drawn; the fog has dissolved everything by then. */
const float FT_FAR = 12.0;

// ---- camera ----------------------------------------------------------------

/** Focal length in view() units: a wide lens, because a tunnel is looked down, not at. */
const float FT_FOCAL = 1.35;

/** How far down the path the camera looks; it banks into a bend that far ahead. */
const float FT_LOOK = 2.5;

/** The slow roll of the frame about the direction of flight, in radians. */
const float FT_ROLL = 0.28;

/** How far the beat throws the camera forward, in world units. */
const float FT_LURCH = 0.18;

/** How far a finger drags the bore sideways, and the cap that keeps the camera inside it. */
const float FT_TOUCH_STEER = 0.35;
const float FT_STEER_MAX = 0.45;

// ---- look ------------------------------------------------------------------

/** Where the palette is read: the base, the spread over the two traps, and the drift along the bore. */
const float FT_HUE_BASE = 0.05;
const float FT_HUE_TRAP_R = 0.55;
const float FT_HUE_TRAP_X = 0.22;
const float FT_HUE_Z = 0.18;
const float FT_HUE_LOBES = 5.0;

/** Palette coordinate of the light at the end of the tunnel. */
const float FT_HUE_END = 0.62;

/** How much of the end light the fog carries, and how fast it fills in per world unit. */
const float FT_FOG_LEVEL = 0.55;
const float FT_FOG_RATE = 0.2;

/** How tightly the ray-integrated glow hugs the shells (per world unit), and its gain. */
const float FT_GLOW_TIGHT = 12.0;
const float FT_GLOW_GAIN = 5.0;

/** Ambient occlusion off the march step count: the free allowance, the slope and the floor. */
const float FT_AO_FREE = 0.30;
const float FT_AO_DEPTH = 1.1;
const float FT_AO_FLOOR = 0.18;

/** Sharpness of the bright core at the vanishing point. */
const float FT_CORE_POWER = 40.0;

/** Tone-map exposure. */
const float FT_EXPOSURE = 1.35;

// ---- per-frame state shared with map() ------------------------------------
//
// Everything here is computed once in main() and read by map(); GLSL ES has
// no way to hand a closure to a distance function, so this is the same
// file-scope idiom kifs_frag and noneuclid_frag use. gOrb, gFoamD and
// gFoamMask run the other way: they are how map() reports its orbit trap
// and its foam distance back, and every map() call overwrites them, so a
// hit's values must be captured BEFORE the normal is taken.

/** Corridor radius this frame. */
float gCorridor;
/** Inversion radius squared this frame. */
float gInvS;
/** Runtime inversion count, below FT_MAX_ITERS. */
float gIters;
/** The rotation between the inversions. */
mat2 gFoldRot;
/** Where the packing has slid to, in foam units. */
vec3 gShift;
/** Rotation of the whole cross-section about the bore. */
float gSpin;
/** Wedges the cross-section is folded into; below 2 there is no fold. */
float gWedges;
/** Sideways drag of the bore under a finger, in world units. */
vec2 gSteer;
/** The orbit trap: min |q| per axis in .xyz and min r2 in .w, over the chain. */
vec4 gOrb;
/** The foam's own distance at the last map() call, before the bore was applied. */
float gFoamD;
/** 1 where the foam is real, fading to 0 inside the bore where it was carved away. */
float gFoamMask;

/** The tunnel axis: where the bore's centre is at depth z, periodic in FT_PERIOD. */
vec2 tunnelPath(float z) {
    float w = FT_TAU / FT_PERIOD;
    return FT_BEND * vec2(sin(z * w * FT_BEND_LOBES_X), 0.8 * sin(z * w * FT_BEND_LOBES_Y + 1.3));
}

/**
 * The foam: the plane y = 0 pulled back through the translate/invert chain.
 * q is in foam units and the result is too, times FT_DE_FUDGE.
 */
float foam(vec3 q) {
    float scale = 1.0;
    gOrb = vec4(FT_TRAP_FAR);
    for (int i = 0; i < FT_MAX_ITERS; i++) {
        if (float(i) >= gIters) break;
        // Domain repetition into the unit cell [-1, 1)^3: a translation, so
        // an isometry, and the first operation of the chain - which is what
        // makes the whole foam periodic with period 2 in every axis.
        q = 2.0 * fract(0.5 * q + 0.5) - 1.0;
        // The rotation between the folds. An isometry, so it costs the scale
        // factor nothing, and it is what stops every level of the packing
        // from landing square on the previous one.
        q.xy = gFoldRot * q.xy;
        float r2 = dot(q, q);
        // The traps are read HERE, in the cell frame before the inversion
        // throws the point outward, so every level measures the same thing.
        gOrb = min(gOrb, vec4(abs(q), r2));
        // Sphere inversion about the cell centre, floored (see FT_INV_FLOOR).
        float k = gInvS / max(r2, FT_INV_FLOOR);
        q *= k;
        scale = min(scale * k, FT_SCALE_CAP);
    }
    return FT_DE_FUDGE * abs(q.y) / scale;
}

/** The scene: the foam, in the tunnel's frame, with the corridor bored out of it. */
float map(vec3 p) {
    // Into the tunnel frame: the bore's centre to the origin, the finger's
    // drag applied, then the twist, so the cross-section turns with depth.
    vec2 c = p.xy - tunnelPath(p.z) - gSteer;
    c = rot2(FT_TWIST * p.z + gSpin) * c;
    float r = length(c);
    // Positive inside the bore: the distance to its wall, which is a valid
    // lower bound on the distance to anything, since everything was carved
    // out of the inside.
    float bore = gCorridor - r;
    if (bore > FT_BORE_MARGIN) {
        gOrb = vec4(FT_TRAP_FAR);
        gFoamD = FT_TRAP_FAR;
        gFoamMask = 0.0;
        return bore;
    }
    // The wedge fold: rotate into the wedge, reflect in its bisector. A
    // piecewise isometry. atan(0, 0) is undefined and one NaN here poisons
    // every step behind it; on the axis the fold is the identity anyway.
    if (gWedges >= 2.0 && r > 1.0e-6) {
        float seg = FT_TAU / gWedges;
        float a = atan(c.y, c.x);
        a = abs(mod(a, seg) - seg * 0.5);
        c = vec2(cos(a), sin(a)) * r;
    }
    vec3 q = vec3(c, p.z) * FT_FOAM_FREQ + gShift;
    // Back to world units, then thickened into a shell.
    float d = foam(q) / FT_FOAM_FREQ - FT_SHELL;
    gFoamD = d;
    gFoamMask = 1.0 - smoothstep(0.0, 0.15, bore);
    // Intersect with the outside of the corridor. max() of two lower bounds
    // is a lower bound on the distance to the intersection, so a step by it
    // is safe even where it carries the ray across the (empty) bore wall.
    return max(d, bore);
}

/**
 * Tetrahedral normal: four map() evaluations rather than the six a central
 * difference needs. The degenerate case is real - four equal taps give the
 * zero vector, and normalize(vec3(0.0)) is NaN - so it falls back to the
 * direction handed in, which is the one facing the camera.
 */
vec3 foamNormal(vec3 p, float e, vec3 fallback) {
    vec2 k = vec2(1.0, -1.0);
    vec3 g = k.xyy * map(p + k.xyy * e) +
        k.yyx * map(p + k.yyx * e) +
        k.yxy * map(p + k.yxy * e) +
        k.xxx * map(p + k.xxx * e);
    float len = length(g);
    return len > 1.0e-20 ? g / len : fallback;
}

void main() {
    // view() first: zoom, rotation, drift, kaleidoscope, tiling, pixelate,
    // shake, twist, warp, ripple, morph and the beat pulse all live in there,
    // and a style that builds its own screen coordinates silently drops
    // fifteen of the user's controls. For a raymarcher they act on the ray
    // direction field, which is the honest 3D reading of each of them.
    vec2 uv = view();

    // Clamped once. The audio uniforms are 0..1.5 and auto-gained; the ceiling
    // stops a loud transient from taking a coefficient somewhere its constant
    // was not chosen for.
    float bassA = min(uBass, 1.3);
    float midA = min(uMid, 1.3);
    float trebA = min(uTreble, 1.3);
    float enA = min(uEnergy, 1.3);

    // The house beat idiom: the ramp's bump times the SQUARED envelope, so it
    // peaks exactly on the heard transient and is silent between hits rather
    // than throbbing through silence at whatever the phase clock free-runs at.
    float beatEnv = clamp(uBeat, 0.0, 1.0);
    float beatBump = pow(0.5 + 0.5 * cos(FT_TAU * uBeatPhase), 2.0);
    float hit = beatEnv * beatEnv * beatBump;
    float beatGain = clamp(uBeatResponse, 0.0, 2.0);

    // ---- touch ------------------------------------------------------------
    //
    // Every term here is exactly zero on an untouched frame: touchAnchor()
    // and touchStrength() return literal zeros, and uTouchAxis and uTouchSpin
    // are written as literal zeros, so the frame is bit-identical to one from
    // a build with no touch at all.
    //
    // One finger drags the bore sideways, so the tunnel bends toward the
    // fingertip. Capped below the corridor radius: a gesture may steer the
    // tunnel, it may not put the camera inside its wall.
    vec2 steer = touchAnchor() * (FT_TOUCH_STEER * touchStrength());
    float steerLen = length(steer);
    if (steerLen > FT_STEER_MAX) steer *= FT_STEER_MAX / steerLen;
    gSteer = steer;
    // Two fingers open the bore. uTouchAxis is a literal zero vector with
    // fewer than two down, so this is an exact 0 then.
    float pinch = clamp(length(uTouchAxis) * 0.62, 0.0, 1.0);
    // Three or more spin the cross-section. TouchField's spin is a rate and a
    // flick can reach its own +-8 limit, so it is clamped before use.
    float spin = clamp(uTouchSpin, -6.0, 6.0);

    // ---- the world this frame ---------------------------------------------

    gCorridor = FT_CORRIDOR * (1.0 + FT_CORRIDOR_PINCH * pinch) + FT_CORRIDOR_BASS * bassA;
    gInvS = FT_INV_BASE
        + FT_INV_SWING * sin(uTime * FT_RATE(19.0))
        + FT_INV_BASS * bassA
        + FT_INV_BEAT * hit * beatGain;
    float foldAngle = FT_FOLD_SWING * sin(uTime * FT_RATE(31.0)) + FT_FOLD_MID * midA;
    gFoldRot = rot2(foldAngle);
    gShift = vec3(
        FT_SLIDE * sin(uTime * FT_RATE(17.0)),
        FT_SLIDE * sin(uTime * FT_RATE(11.0) + 2.0),
        0.0
    );
    // The spin's free-running term completes 23 turns per wrap, so it is
    // continuous across the restart like every other rate here.
    gSpin = uTime * FT_RATE(23.0)
        + FT_SPIN_MID * midA
        + FT_SPIN_BEAT * hit * beatGain
        + FT_TOUCH_SPIN * spin;
    // The user's Symmetry/Kaleidoscope controls, in 3D: view() has already
    // folded the SCREEN into uSymmetry wedges when Kaleidoscope is on, and
    // this folds the cross-section of the WORLD into the same number, so the
    // tunnel itself gains the symmetry rather than a mirrored photograph of
    // it. With Kaleidoscope off the style folds into FT_WEDGES of its own;
    // Kaleidoscope on with Symmetry set to 0 is the one way to see the
    // unfolded spiral.
    gWedges = (uKaleido > 0.5) ? uSymmetry : FT_WEDGES;
    // Detail buys inversions as well as march steps: uSteps runs 64..128, and
    // the extra levels of the packing are what the extra steps are for. The
    // beat adds exactly one - the only discrete thing on the frame - scaled
    // by the user's Beat response so that setting it to zero really stops it.
    // One more inversion adds spheres finer than everything already on
    // screen inside the cavities, so the silhouette does not move.
    float detail = clamp((uSteps - 64.0) / 64.0, 0.0, 1.0);
    float iterBeat = step(0.30, hit * beatGain);
    gIters = FT_ITERS_BASE + floor(detail * 2.0 + 0.5) + iterBeat;

    // ---- camera -----------------------------------------------------------
    //
    // The camera sits on the tunnel axis and falls along it. mod() keeps z
    // small so the fold below never loses float precision, and FT_FLY makes
    // the wrap a period boundary so the mod is invisible.
    float zCam = mod(uTime * FT_FLY, FT_PERIOD) + FT_LURCH * hit * beatGain;
    vec3 ro = vec3(tunnelPath(zCam), zCam);
    float zLook = zCam + FT_LOOK;
    vec3 fwd = normalize(vec3(tunnelPath(zLook), zLook) - ro);
    // The roll hint stays in the xy plane and fwd is never far from +z, so
    // the basis below cannot degenerate.
    float roll = FT_ROLL * sin(uTime * FT_RATE(9.0));
    vec3 upHint = vec3(sin(roll), cos(roll), 0.0);
    vec3 right = normalize(cross(upHint, fwd));
    vec3 up = cross(fwd, right);
    vec3 rd = normalize(right * uv.x + up * uv.y + fwd * FT_FOCAL);

    // The pixel footprint per unit of ray length: view() is height-normalized,
    // so one pixel is 2/height in uv units, and a uv offset of that at focal
    // length FT_FOCAL opens by the same fraction of the distance marched.
    float epsPerT = FT_EPS_PIXELS * 2.0 / (max(uResolution.y, 1.0) * FT_FOCAL);

    // Per-pixel start offset, seeded on the pixel only: the glow below is an
    // integral along the ray, and sampling every pixel at the same depths
    // quantizes it into concentric shells. A time-varying dither on a
    // fullscreen pass is flicker, and this app has a photosensitivity budget.
    float jitter = hash13(vec3(gl_FragCoord.xy, 3.0));
    float t = 0.02 + jitter * 0.02;

    // ---- march ------------------------------------------------------------

    float hitT = -1.0;
    float used = 0.0;
    vec4 hitOrb = vec4(FT_TRAP_FAR);
    // The ray-integrated glow: a weight and a weighted trap. The colour is
    // resolved ONCE after the loop rather than by calling pal() per step.
    float glowSum = 0.0;
    float glowTrap = 0.0;

    for (int i = 0; i < FT_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        // Steps CONSUMED, for the occlusion. The hit branch overwrites it with
        // the steps taken BEFORE landing.
        used = float(i) + 1.0;
        vec3 p = ro + rd * t;
        float d = map(p);
        // Epsilon grows with the distance marched: a constant one shimmers
        // far down the bore and wastes steps on the near wall.
        float eps = epsPerT * t + FT_EPS_FLOOR;
        if (d < eps) {
            hitT = t;
            used = float(i);
            hitOrb = gOrb;
            break;
        }
        float stepLen = max(d * FT_STEP, eps * 0.5);
        // Everything near a shell leaks light. Weighted by the foam's OWN
        // distance rather than the bore-limited one, and masked inside the
        // bore, or the corridor wall would glow as a cylinder of haze around
        // the camera where shells were carved away.
        float w = exp(-max(gFoamD, 0.0) * FT_GLOW_TIGHT) * stepLen * gFoamMask;
        glowSum += w;
        glowTrap += w * sqrt(min(gOrb.w, FT_TRAP_FAR));
        t += stepLen;
        if (t > FT_FAR) break;
    }

    float budget = max(uSteps, 1.0);

    // The light at the end of the tunnel. A core where the view lines up with
    // the direction of flight, in the user's palette with a white heart, and
    // a dim wash of the same colour everywhere a ray escapes. Energy lifts
    // the heart a little; it is a small fixed area, so it stays inside the
    // flash budget.
    float core = pow(clamp(dot(rd, fwd), 0.0, 1.0), FT_CORE_POWER);
    vec3 endLight = pal(FT_HUE_END + 0.08 * sin(uTime * FT_RATE(7.0)));
    vec3 sky = endLight * (0.12 + 0.55 * core) + vec3(1.0) * core * core * (0.35 + 0.45 * enA);
    vec3 fogCol = endLight * FT_FOG_LEVEL;
    vec3 col;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        // Sampled slightly wider than the surface epsilon: the epsilon is where
        // the ray was allowed to stop, the normal is what the pixel is shaded
        // with, and the pixel is the wider of the two. Still under FT_SHELL
        // in the near field, so the taps never straddle a shell's mid-plane.
        float px = epsPerT * hitT + FT_EPS_FLOOR;
        vec3 n = foamNormal(p, px * 1.3, -rd);

        // Ambient occlusion, free: how much of its budget the ray spent. A ray
        // that needed most of it was crawling between shells, which is where
        // the crevices are; the allowance is the baseline a clean hit costs.
        float ao = clamp(1.0 - FT_AO_DEPTH * max(used / budget - FT_AO_FREE, 0.0), FT_AO_FLOOR, 1.0);

        // The orbit trap as a palette coordinate. Two traps: the closest
        // approach to a cell centre bands the packing into its nested
        // levels, and the x trap cuts across those bands so two shells at the
        // same level are still told apart. A slow drift along the bore on top,
        // so the tunnel changes colour as it is fallen down - rings of it, the
        // way a spiral tunnel reads.
        float trapR = clamp(sqrt(min(hitOrb.w, FT_TRAP_FAR)) * 0.75, 0.0, 1.0);
        float trapX = clamp(hitOrb.x * 1.4, 0.0, 1.0);
        float zHue = FT_HUE_Z * sin(p.z * (FT_TAU / FT_PERIOD) * FT_HUE_LOBES);
        float hue = FT_HUE_BASE + FT_HUE_TRAP_R * trapR + FT_HUE_TRAP_X * trapX + zHue + 0.05 * midA;
        vec3 body = pal(hue);
        // A related but distinct hue for the rim: far enough round the wheel
        // to read as a different material, close enough to be the same shell.
        vec3 rimCol = pal(hue + 0.31);

        // Two lights: the camera's own headlight, and the light at the end of
        // the tunnel, which lights the surfaces that face down the bore. A
        // headlight alone flattens a fold; the second light is what gives the
        // cut spheres their far side.
        float head = clamp(dot(n, -rd), 0.0, 1.0);
        float back = clamp(dot(n, fwd), 0.0, 1.0);
        float fres = pow(1.0 - head, 3.0);
        // Treble sharpens the highlight: a tighter lobe reads as a harder,
        // glassier bead without touching the geometry, which is the only
        // thing a per-frame treble value is safe to drive.
        float spec = pow(clamp(dot(n, normalize(fwd - rd)), 0.0, 1.0), 16.0 + 60.0 * trebA);

        // Each level of the packing is lit by its own band of the spectrum,
        // so a bassline lights the deep shells and a hi-hat the near ones
        // instead of everything brightening at once. 0.5 is the floor, so
        // with silence the rim is still there.
        float lit = aband(trapR);

        col = body * (0.20 + 0.55 * head + 0.35 * back) * ao;
        col += rimCol * fres * (0.35 + 0.5 * trebA) * (0.5 + 0.9 * lit) * ao;
        col += mix(vec3(1.0), rimCol, 0.6) * spec * (0.25 + 0.35 * trebA) * ao;

        // Into the end light with distance, so the far rings dissolve into
        // the glow rather than into black: that is what makes it a tunnel
        // with a light at the end instead of a cave.
        col = mix(col, fogCol, 1.0 - exp(-hitT * FT_FOG_RATE));
    } else {
        // A ray that escaped past FT_FAR saw the end light; one that ran out
        // of budget was still deep between shells, and gets the fog there.
        col = (t > FT_FAR) ? sky : fogCol;
    }

    // The glow, coloured once. uEnergy is the density control by convention;
    // the floor keeps it present in silence, where it is what gives the gaps
    // between the shells their depth. Through 1 - exp(-x) rather than added
    // raw: the raw sum is an unbounded path integral, and a spike in a
    // fullscreen additive term is exactly what the flash budget exists to
    // keep out.
    float norm = max(glowSum, 1.0e-6);
    float glowHue = FT_HUE_BASE + FT_HUE_TRAP_R * clamp((glowTrap / norm) * 0.75, 0.0, 1.0) + 0.2;
    col += pal(glowHue) * (1.0 - exp(-glowSum * FT_GLOW_GAIN)) * (0.22 + 0.4 * enA) * (1.0 + 0.3 * hit * beatGain);

    // The wake: a soft lift under every finger, live and still fading.
    // touchWake() is unbounded above by design (five fingers sum to five), so
    // it goes through 1 - exp(-x) before it is allowed near the frame.
    // Exactly zero when nothing is touching.
    float wake = 1.0 - exp(-touchWake(uv));
    col += pal(0.5 + 0.2 * enA) * wake * 0.16;

    // Three additive layers, so this is HDR by construction. The tone map
    // keeps the rims and the glow apart instead of clipping them to the same
    // white, and bounds the whole frame's luminance whatever the music does.
    col = vec3(1.0) - exp(-max(col, vec3(0.0)) * FT_EXPOSURE);

    // Vignette in SCREEN space, off vUv rather than off view()'s uv, so it is
    // not a function of the Zoom control.
    vec2 nd = vUv * 2.0 - 1.0;
    col *= 1.0 - 0.22 * dot(nd, nd) * 0.5;

    fragColor = vec4(grade(col), 1.0);
}
