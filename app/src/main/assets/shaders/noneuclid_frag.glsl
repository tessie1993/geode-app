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

// NONEUCLID - space turning inside-out through a point.
//
// ---- what is actually being drawn -----------------------------------------
//
// One small prototype - a cubic lattice of struts with a bead at every node -
// seen through a chain of alternating BOX FOLDS and SPHERE INVERSIONS. That
// chain is the Kleinian/Apollonian construction: the box fold is a reflection
// (a translation generator's mirror), the sphere fold is inversion in a sphere
// (p -> p * r2/dot(p,p)), and the group they generate has a limit set that
// nests forever. The lattice is the only thing with any shape in this file;
// everything the eye reads as structure is what the group does to it.
//
// Inversion is the transform that swaps the inside of a sphere with the whole
// of the rest of space, which is why the picture cannot be read as a Euclidean
// object seen from an angle: the struts are straight in the folded frame and
// arrive on screen as ARCS, and their spacing changes as they approach a pole
// instead of foreshortening. That is the entire visual signature, and it is
// why the prototype is a lattice of straight lines rather than something that
// is already curved - a curved prototype would hide the transform.
//
// ---- why the estimate is safe to march ------------------------------------
//
// Every step of the chain is either an isometry (the box fold, the fold-basis
// rotation and offset, the world rotation) or a CONFORMAL map with a known
// scalar local scale factor (the two inversions). So the composition is
// conformal with scale s = product of those factors, and
//
//     distance in world units ~= lattice(folded point) / s
//
// which is the same accounting hyperspace_frag's deGasket/deCoral do. Two
// details keep it honest:
//
// - both inversions are FLOORED at a minimum radius. Below that floor the map
//   is a plain uniform scale, whose second derivative is zero - so the region
//   where the conformal pullback would be least accurate is exactly the region
//   where it becomes exact. Without the floor the reciprocal returns inf at
//   the pole and s goes to NaN, and one NaN distance is not a bad pixel: the
//   ray never terminates and the frame time collapses.
// - s is capped at 1e30. At the gains this style settled on it cannot get
//   near that (six folds of at most 1.85 reach 40), so the cap is a guard
//   rather than a working limit - but it is the guard that matters, because an
//   infinite s makes the estimate 0/inf = 0, i.e. a surface reported where
//   there is none, and the gains are audio-driven. An underestimate is safe;
//   the march just creeps and the epsilon catches it.
//
// The march still steps a fraction of the estimate (STEP_SCALE), because a
// conformal pullback is only first-order.
//
// ---- touch: the finger IS the pole ----------------------------------------
//
// There is always a global inversion pole in the scene, drifting slowly on its
// own so an untouched frame still has a lens of inside-out space wandering
// through the lattice. A finger takes it over: the pole eases to the touch
// point, so the world turns inside out about wherever you put your hand and
// dragging drags the singularity through the structure. Two fingers set the
// pole's radius (pinch the bubble open and shut), three or more rotate the
// fold basis. Every one of those terms is exactly zero when nothing is being
// touched, so an untouched frame is bit-identical to one from a build without
// the touch uniforms.

// ---- caps ------------------------------------------------------------------
//
// Compile-time bounds, in the shape lib_sdf3's RAYMARCH note requires: the
// bound is what the driver unrolls against, the break is what makes the common
// ray cheap. MAX_STEPS is MarchBudget.MAX_STEPS, the ceiling uSteps is handed
// out of; MAX_FOLDS is this style's own, and the runtime fold count breaks on
// gIters below it.
#define MAX_STEPS 128
#define MAX_FOLDS 6

const float TAU = 6.2831853;

// ---- camera ----------------------------------------------------------------
//
// The eye sits at the origin and looks down +z, so the ray frame IS the frame
// lib_touch documents for a screen anchor: x and y are the screen axes in
// view() units, z runs along the view direction. That is what lets a 2D finger
// position become a 3D pole with one divide (see gPole in main()), and it is
// the reason the camera is built this way round rather than as a free-flying
// eye with a basis matrix.
//
// FOCAL 1.6 is a 64-degree vertical field. Together with the distance and
// radius below the structure subtends about 88% of the half-height at the
// default Zoom - full enough to be the subject, with enough margin that the
// silhouette reads as a silhouette instead of being cropped by the frame.
const float FOCAL = 1.6;
/** Eye-to-structure distance. */
const float CAM_DIST = 4.05;
/** Radius of the ball the whole construction is clipped to. */
const float BALL_R = 1.95;
/**
 * Gate width on the bounding sphere.
 *
 * A gate at exactly zero makes the march creep: the step lands on the sphere,
 * the bound is zero, and the ray never gets inside. Same reason
 * hyperspace_frag carries uBoundMargin.
 */
const float BOUND_MARGIN = 0.06;
/** Nothing past this is drawn; the ball ends at CAM_DIST + BALL_R = 6.0. */
const float FAR = 7.2;
/**
 * Fraction of the estimate a step takes.
 *
 * A little under hyperspace's 0.82 because there are TWO inversion families
 * here (the pole and the chain) and the conformal pullback is only
 * first-order, so between the radius floors the curvature term is real.
 *
 * Honestly reported: this is headroom, not a fix for anything observed.
 * Halving it to 0.45 was tried while chasing the style's speckle and changed
 * the picture almost not at all, which is what ruled overshoot out and sent
 * the search to the glow integral, where the fault actually was. 0.72 is kept
 * because the margin is nearly free and the estimate is not exact.
 */
const float STEP_SCALE = 0.72;
/**
 * Surface epsilon as a multiple of the PIXEL FOOTPRINT, not a constant.
 *
 * This one is not a tuning knob, it is the band limit. A Kleinian chain has
 * detail at every scale, so wherever the accumulated inversion factor s is
 * large the finest feature is smaller than a pixel - and a fixed epsilon
 * resolves it anyway, giving the normal of whichever sub-pixel fragment the
 * ray happened to land on. Stopping the ray at the pixel's own width limits
 * the geometry to what the screen can show, which is both correct and cheaper.
 *
 * It is worth saying what this did NOT fix, since it was reached for first:
 * the style's speckle survived it untouched, because the speckle was in the
 * ray-integrated glow (see GLOW_TIGHT) and not in the surface at all. This
 * still belongs here - it is what keeps the grazing edges and the deep folds
 * from shimmering as the structure turns - but it was not the artefact.
 *
 * 1.2 rather than 1.0 because the footprint is the CENTRE-to-centre spacing
 * and a ray grazing a strut needs a little more slack than that before it
 * stops chasing detail it cannot display. It does not go higher: the same
 * number sets the width the NORMAL is sampled at, and once that approaches a
 * strut's own thickness the four taps straddle the strut, the gradient cancels
 * and the surface shades black. Wide enough to band-limit, narrow enough to
 * still be measuring the strut.
 */
const float EPS_PIXELS = 1.2;
/** Floor, for the near field where the footprint goes to nothing. */
const float EPS_FLOOR = 6.0e-4;

// ---- the prototype ---------------------------------------------------------
//
// The lattice period, in the folded frame's units.
//
// This and the fold count together decide whether the style reads as geometry
// or as mush, and both had to be found by looking. At 1.0, with the seven
// folds this began with, the ball was four cells across at the coarsest level
// and every fold packed more in: the eye was shown six levels of nesting at
// once and could resolve none of them. At 1.55 the top level is two and a bit
// cells across, so ONE strut is a recognisable object that the folds visibly
// bend into an arc - which is the whole point of the style. Fewer, larger
// cells; the nesting then reads as depth rather than as grain.
const float CELL = 1.55;
/**
 * Strut and bead radii, in the same units. Both are well under CELL * 0.5,
 * which is what makes lattice() exact: the nearest member of a family is
 * always the one in your own cell.
 *
 * The strut is 7% of a cell, so a face is more than four fifths open. That
 * openness is not decoration - it is what lets a ray reach the second and
 * third level of the nesting, and a fatter strut closes the structure into a
 * solid that could be any fractal at all.
 */
const float CELL_STRUT = 0.115;
const float CELL_BEAD = 0.235;
/** Weld radius at the nodes. Small enough that the joint reads as a joint;
 *  smin's blend is not associative, so bead-then-struts is the shape. */
const float CELL_WELD = 0.11;

// ---- the folds -------------------------------------------------------------
/** Half-size of the box fold. 1.0 puts the mirror planes on the unit cube. */
const float BOX_FOLD = 1.0;
/** Radius^2 of the chain's sphere fold, before bass. */
const float INV_R2 = 0.96;
/**
 * Floor on the chain's inversion, as a FRACTION of its radius^2 - so it is the
 * per-fold magnification that is pinned (1/0.54 = 1.85) and the radius is free
 * to move with the music. A fixed floor would have made bass raise the gain as
 * well as the size, i.e. one control doing two things, and the second of them
 * is the one that decides how fine the structure is.
 *
 * 1.85 per fold is the style's detail governor. Six folds reach about 40, so
 * the finest feature stays near the pixel rather than far below it - the
 * world-size of a strut is CELL / s, and at the 3.4 per fold this started with
 * the deep regions were a thousandth of the frame across. The picture wants
 * inversion's CURVATURE, which is present at any gain; it does not want the
 * magnification run to the limit.
 *
 * It is also the radius inside which the map becomes a pure uniform scale -
 * see the header note on why that is where the estimate needs it most.
 */
const float INV_FLOOR = 0.54;
/** Radius^2 of the global pole, before bass and pinch. */
const float POLE_R2 = 0.62;
/**
 * Floor on the pole, as a fraction of its radius^2, same job and same
 * reasoning as INV_FLOOR. 1/0.22 = 4.5, more than twice the chain's per-fold
 * gain, because the bubble has to read as a different world rather than as a
 * dent - and it can afford to, being one inversion rather than six compounded.
 * Pinching the bubble open then changes its SIZE and not its violence, which
 * is the gesture the user thinks they are making.
 */
const float POLE_FLOOR = 0.22;

// ---- look ------------------------------------------------------------------
/**
 * How tightly the ray-integrated glow hugs the surface, per world unit.
 *
 * This is a smoothing constant, not a look constant, and it is why it is 3.5
 * and not the 9 it started at. The glow is an integral along a ray through a
 * FRACTAL near-field: at 9 the weight is effectively a thin shell around the
 * geometry, two neighbouring rays thread that shell differently, and the term
 * came out as per-pixel confetti over the whole middle distance - the single
 * worst artefact this style had, and it was in the haze rather than in the
 * surface the whole time. A broader kernel integrates over enough of the field
 * that neighbouring rays agree, which is what a haze is supposed to do.
 */
const float GLOW_TIGHT = 3.5;
/**
 * Distance haze, per world unit. The body is about four units deep, so its far
 * side arrives at roughly 60% of its own colour - enough that depth reads,
 * little enough that the back of the structure is still structure.
 */
const float FOG_RATE = 0.115;
/** Tone-map exposure. See the note where it is applied. */
const float EXPOSURE = 1.30;

// ---- per-frame constants, and the two orbit traps --------------------------
//
// Everything here is computed once in main() and read by map(); GLSL ES has no
// way to hand a closure to a distance function, so this is the same file-scope
// idiom hyperspace_frag uses for gTrap/gHue.
/** Centre of the bounding ball, in the camera frame. Its radius is the
 *  constant BALL_R - nothing modulates it, because the ball is the frame the
 *  composition is built in and the music moving it would move the subject. */
vec3 gCenter;
/** The inversion pole, in the camera frame. */
vec3 gPole;
float gPoleR2;
/** Chain parameters. */
float gInvR2;
float gIters;
mat3 gFoldRot;
vec3 gFoldOff;
mat3 gWorldRot;
float gBead;
/**
 * The orbit traps: the closest the folded point ever came to the origin and to
 * the y axis, both kept SQUARED so the loop never pays a sqrt. A fractal
 * surface coloured by depth alone reads as noise; coloured by what its orbit
 * did it reads as nested shells, which is the difference between a texture and
 * a structure. Written by map(), so anything that calls map() again - the
 * normal, the occlusion - overwrites them: capture before shading.
 */
float gTrapO;
float gTrapA;
/** The pixel footprint per unit of distance marched - see EPS_PIXELS. */
float gEpsPerT;

/**
 * The prototype: an infinite cubic lattice of struts with a bead at each node.
 *
 * Exact and Lipschitz-1, which matters because it is the only thing dividing
 * by the accumulated scale - an overestimate here is multiplied by s and walks
 * the ray through the whole construction. It is exact because each family is
 * evaluated on the cell-relative coordinate and every radius is under half a
 * cell, so the nearest member of a family is always the one in your own cell:
 * a tube along x depends only on (y,z), so `length(c.yz) - r` is the distance
 * to the whole infinite family and not just to a neighbour.
 *
 * fract() rather than round(): round()'s behaviour at exactly .5 is not pinned
 * down, and a half-cell plane that resolves one way on one driver and the
 * other way on another is a seam through the middle of every cell.
 */
float lattice(vec3 p) {
    vec3 c = (fract(p / CELL + 0.5) - 0.5) * CELL;
    float bead = length(c) - gBead;
    float struts = min(length(c.yz), min(length(c.xz), length(c.xy))) - CELL_STRUT;
    return smin(bead, struts, CELL_WELD);
}

/**
 * The scene: the Kleinian chain, clipped to its bounding ball.
 *
 * Order is deliberate. The pole inversion runs in the CAMERA frame, before the
 * world rotation, so the singularity stays under the finger while the
 * structure turns behind it - inverting after the rotation would drag the pole
 * around the screen once every world revolution.
 */
float map(vec3 p) {
    // Outside the ball the distance to the ball is already a valid lower bound
    // on the distance to anything in it, so the fold chain is never touched
    // and the empty half of the frame costs one length() per step.
    float bound = sdSphere(p - gCenter, BALL_R);
    if (bound > BOUND_MARGIN) {
        // The traps still have to be defined: main() reads them after every
        // map() call to colour the ray-integrated glow, and a stale pair would
        // paint the void in the colour of whatever the ray last passed.
        gTrapO = 1.0;
        gTrapA = 1.0;
        return bound;
    }

    // The running conformal scale. Both inversions multiply into it and the
    // final estimate is divided by it; see the header.
    float s = 1.0;

    // ---- the global pole: the inside of this sphere IS the outside ---------
    //
    // Only points INSIDE the pole sphere are moved. That is what makes it read
    // as a lens set into the structure rather than as the whole frame sliding:
    // the lattice around the bubble stays put, so the eye has an undistorted
    // reference to measure the distortion against, and the boundary is
    // continuous because k -> 1 exactly as r2 -> gPoleR2 from below.
    vec3 v = p - gPole;
    float pr2 = dot(v, v);
    float pk = pr2 < gPoleR2 ? gPoleR2 / max(pr2, gPoleR2 * POLE_FLOOR) : 1.0;
    vec3 q = gPole + v * pk;
    s *= pk;

    // Into the structure's own frame. A rotation and a translation, so the
    // bounding sphere tested above is still the sphere this is inside of.
    q = gWorldRot * (q - gCenter);

    gTrapO = 1.0e9;
    gTrapA = 1.0e9;
    for (int i = 0; i < MAX_FOLDS; i++) {
        if (float(i) >= gIters) break;
        // Box fold: reflect anything outside the cube back in. A reflection,
        // so it costs the scale factor nothing. Written as clamp*2 - p rather
        // than as a branch because the branch is non-uniform across the quad
        // and the arithmetic form is the same three operations either way.
        q = clamp(q, -BOX_FOLD, BOX_FOLD) * 2.0 - q;

        float r2 = dot(q, q);
        // The traps are read HERE, between the two folds, because this is the
        // one point in the iteration where the orbit is in the frame the group
        // is defined in - after the inversion it has been thrown outward and
        // the same shell means a different thing every iteration.
        gTrapO = min(gTrapO, r2);
        gTrapA = min(gTrapA, dot(q.xz, q.xz));

        // Sphere fold. Three regimes, continuous across both joins: identity
        // outside, inversion between the radii, uniform scale inside the
        // floor.
        float k = r2 < gInvR2 ? gInvR2 / max(r2, gInvR2 * INV_FLOOR) : 1.0;
        q *= k;
        s = min(s * k, 1.0e30);

        // Rotate the fold basis and step it. Both isometries, so the whole
        // chain's scale factor is still just the product of the inversions.
        // The rotation is what stops the group from being a lattice of its own
        // - with an axis-aligned basis every level lands on the previous one
        // and the nesting reads as one repeated cell instead of as depth.
        q = gFoldRot * q + gFoldOff;
    }

    // Pull the prototype's distance back through the accumulated scale, then
    // intersect with the ball. The chain describes an UNBOUNDED set - the
    // lattice is infinite and the folds do not confine a far-away ray - so
    // without the intersection the structure streaks off across the whole
    // frame and there is no silhouette and no space to see it in.
    return max(lattice(q) / s, bound);
}

/**
 * The tetrahedral 4-tap - four map() calls rather than a central difference's
 * six, and symmetric, so a flat face reads flat - returning the normal in .xyz
 * AND how much that normal can be trusted in .w.
 *
 * The .w is the whole reason this is one function instead of two. A distance
 * field has |grad| = 1 by definition, and for this tap pattern the raw sum has
 * length exactly 4 * eps when the field is locally linear (the four tetrahedron
 * vertices satisfy sum(k) = 0 and sum(k k^T) = 4I, so the sum is 4 * eps *
 * grad). Where the surface is finer than the taps, the four taps decorrelate,
 * they partly cancel, and the sum comes back SHORT. So the shortfall measures
 * exactly the thing that ruins the picture - a Kleinian chain has detail at
 * every scale, and the deep regions of it are finer than any screen - and it
 * measures it at the tap scale, i.e. at the pixel, which is where the question
 * is asked.
 *
 * It is measured rather than predicted. The obvious prediction - the smallest
 * feature is CELL divided by the accumulated inversion scale, compare that
 * with the pixel - was written first and thrown away: it says nothing about
 * the fold planes, where the composition is only piecewise smooth and where
 * the surface actually goes bad, and on a real frame it fired almost nowhere.
 * The taps are already being paid for; asking them what they found is free.
 */
vec4 fieldAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    vec3 g = k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps) +
        k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps);
    float len = length(g);
    // The degenerate case is real: four equal taps give the zero vector, and
    // normalize(vec3(0.0)) is NaN, which reaches the framebuffer as a black
    // pixel that no amount of grading can explain.
    vec3 n = len > 1e-20 ? g / len : vec3(0.0, 1.0, 0.0);
    return vec4(n, len / (4.0 * eps));
}

/**
 * Two-tap ambient occlusion, written out rather than looped.
 *
 * Each tap is a whole map() - a whole fold chain - so a third tap is a real
 * cost for a term the eye reads as "the creases are darker". Two is enough to
 * seat the struts into their nodes, which is the only place this construction
 * has creases worth seating.
 */
float occlusion(vec3 p, vec3 n, float h) {
    float o1 = (h - map(p + n * h)) / h;
    float o2 = (2.0 * h - map(p + n * 2.0 * h)) / (2.0 * h);
    return clamp(1.0 - 0.75 * (o1 + 0.55 * o2), 0.0, 1.0);
}

/**
 * The void behind the structure.
 *
 * Inverted as well, about the pole's own screen position, so the background
 * gradient's iso-lines are circles through the pole instead of straight bands
 * - the empty part of the frame agrees with the geometry about what straight
 * means. Deliberately dim: this is a full-screen term, and a full-screen term
 * that moved with the music is exactly the shape of thing the photosensitivity
 * budget exists to keep out.
 */
vec3 sky(vec3 rd, vec2 poleScreen) {
    // Project the direction onto the focal plane. The floor on rd.z keeps a
    // grazing ray (the Zoom control can widen the field a long way) from
    // sending this to infinity.
    vec2 sp = rd.xy / max(rd.z, 0.2) - poleScreen;
    // Same floored reciprocal as the geometry, for the same reason: the raw
    // one is inf at the pole, and sin() of inf is not a gradient.
    vec2 inv = sp / max(dot(sp, sp), 0.06);
    float f = 0.5 + 0.5 * sin(inv.x * 1.2 + uTime * 0.061) * cos(inv.y * 1.1 - uTime * 0.047);
    return pal(0.62 + f * 0.16) * (0.042 + 0.055 * f + 0.05 * clamp(uEnergy, 0.0, 1.5));
}

void main() {
    // view() first, so Zoom, Rotation, Drift, Kaleido, Tiling, Pixelate,
    // Shake, Twist, Warp, Ripple, Morph and the beat pulse all reach this
    // style the same way they reach every other one. For a raymarcher they act
    // on the RAY DIRECTION field, which is the honest 3D reading of each: Zoom
    // becomes focal length, Kaleido becomes a mirrored lens, Tiling becomes a
    // wall of copies of the view.
    vec2 uv = view();

    // Audio, normalized out of the 0..1.5 contract and clamped, so every gain
    // below is read against a 0..1 scale.
    float bassN = clamp(uBass, 0.0, 1.5) / 1.5;
    float midN = clamp(uMid, 0.0, 1.5) / 1.5;
    float trebN = clamp(uTreble, 0.0, 1.5) / 1.5;
    float energyN = clamp(uEnergy, 0.0, 1.5) / 1.5;

    // The house beat idiom: the squared envelope times the phase bump, so it
    // is a spike ON the beat and silent between hits even though the phase
    // clock free-runs through silence.
    float beatEnv = clamp(uBeat, 0.0, 1.0);
    float beatBump = pow(0.5 + 0.5 * cos(TAU * uBeatPhase), 2.0);
    float beatHit = beatEnv * beatEnv * beatBump;

    // ---- touch -----------------------------------------------------------
    //
    // Every term here is exactly zero on an untouched frame: touchStrength()
    // returns a literal 0, uTouchAxis and uTouchSpin are written as literal
    // zeros, and mix(a, b, 0.0) is a. So the frame is bit-identical to one
    // from a build with no touch at all.
    //
    // The ease is on the anchor's AGE rather than on a filter, because a
    // fragment shader has no state to filter with. TouchField already ramps
    // the strength down on release, so this only has to handle the other end:
    // 0.18 s is long enough that the pole slides into place instead of
    // teleporting, and short enough that it still feels like a direct grab.
    float grab = touchStrength() * smoothstep(0.0, 0.18, uTouchAnchor.w);
    // Two fingers: the separation opens the pole. uTouchAxis is a literal zero
    // vector with fewer than two down, so this is an exact 0 then.
    float pinch = clamp(length(uTouchAxis) * 0.62, 0.0, 1.0);
    // Three or more: the swirl turns the fold basis. uTouchSpin is a RATE and
    // this uses it as a bounded angular offset rather than integrating it -
    // there is nowhere in a fragment shader to keep the integral, and a
    // stateless one would drift and never come back. As an offset the basis
    // twists while the fingers turn and springs back when they stop, which is
    // the gesture the user is making anyway.
    float spin = clamp(uTouchSpin * 0.55, -0.9, 0.9);

    // ---- the frame's geometry --------------------------------------------
    //
    // Bass pushes the eye in. A camera push is a continuous quantity, so the
    // gain is small: 7% of the standoff at a full-scale low end, which reads
    // as the structure leaning toward you and cannot read as a cut.
    float camZ = CAM_DIST - 0.30 * bassN;
    // A slow lateral wander so the structure is never quite centred. Rates are
    // chosen mutually irrational-ish (0.061 / 0.043) so the two axes do not
    // relock into a straight line every few seconds.
    gCenter = vec3(0.22 * sin(uTime * 0.061), 0.17 * sin(uTime * 0.043 + 1.1), camZ);

    // The pole. Idle, it drifts on its own inside the ball, which is the
    // ambient motion this style has to have with the audio at zero: a lens of
    // inside-out space wandering through the lattice, plus the fold basis
    // turning below.
    vec3 drift = vec3(
        0.55 * sin(uTime * 0.071),
        0.44 * sin(uTime * 0.053 + 2.1),
        0.38 * sin(uTime * 0.037 + 4.2)
    );
    vec3 idlePole = gCenter + drift;
    // The finger's pole. touchAnchor() is in view() units, and a point at
    // depth z lands on screen at xy * FOCAL / z, so this is the inverse of
    // that projection at the structure's own depth: the pole sits exactly
    // under the fingertip.
    //
    // Exactly, that is, for the default view. touchAnchor() is deliberately
    // NOT pushed through view()'s stack (lib_touch says so), and view() is not
    // invertible in closed form once Kaleido, Tiling or Pixelate are folding
    // the plane - so with those on, the pole is placed where the finger is on
    // the SURFACE and the fold decides where that appears. Nothing else is
    // available without duplicating view(), and a second copy of view() is the
    // exact duplication lib_scene_uniforms exists to prevent.
    vec3 grabPole = vec3(touchAnchor() * camZ / FOCAL, camZ);
    gPole = mix(idlePole, grabPole, grab);
    // How big the inside-out region is.
    //
    // Bass is the headline audio mapping and is still only 30%, because the
    // pole's radius is GEOMETRY - it decides how much of the frame the
    // inside-out region covers - and there is no slew-limited envelope in the
    // shared uniform contract to lean on, so the coupling has to be small
    // enough that per-frame jitter stays under the eye's threshold.
    //
    // A finger also OPENS the bubble rather than only moving it. Without that
    // term a touch slid a lens a fifth of the body wide around the frame,
    // which reads as a blemish; at 1.65x the radius it is half the body, and
    // what the user sees is the world turning inside out around their
    // fingertip, which is the gesture this style exists for.
    gPoleR2 = POLE_R2 * (1.0 + 0.30 * bassN + 0.85 * pinch + 0.65 * grab);

    // The chain. Mids steer the fold planes: same argument as above about
    // gain, and mids are the steering band by convention.
    float foldAngle = uTime * 0.041 + midN * 0.45 + spin;
    gFoldRot = rotAxis(vec3(0.37, 0.78, -0.51), foldAngle);
    // The offset breathes slowly, so the group is never the same group twice
    // and the structure keeps reorganising with the audio at zero.
    gFoldOff = vec3(0.62, -0.24, 0.41) * (1.0 + 0.06 * sin(uTime * 0.029));
    gInvR2 = INV_R2 * (1.0 + 0.10 * bassN);
    gWorldRot = rotY(uTime * 0.083) * rotX(0.26 * sin(uTime * 0.037));
    gBead = CELL_BEAD + 0.030 * bassN;

    // Fold count - the one discrete quantity in the style, and the only thing
    // uBeat is allowed to touch.
    //
    // The base tracks the user's Detail through uSteps, because here the
    // per-STEP cost IS the fold chain: a Detail control that only shortened
    // the march would leave the expensive half of the frame untouched. One to
    // three, and the range is not timid. Each fold is an inversion, so it
    // multiplies the structure's spatial frequency by up to 1.85 and folds the
    // whole body once more; at six the body was a granular mass with no
    // readable form left, and at one it is already unmistakably curved. This
    // construction says what it has to say in very few iterations.
    //
    // The beat adds exactly ONE. Two was tried and is too much: it takes the
    // body from three folds to five in a frame, which reorganises the whole
    // silhouette rather than adding a level inside it - a change of AREA, and
    // area is the quantity the photosensitivity budget is about. One fold
    // reads as the structure gaining detail on the downbeat, which is the
    // event that was wanted.
    float detail = clamp((uSteps - 64.0) / 64.0, 0.0, 1.0);
    gIters = clamp(
        1.0 + floor(detail * 2.0 + 0.5) + floor(beatHit * clamp(uBeatResponse, 0.0, 1.0) * 1.99),
        1.0,
        float(MAX_FOLDS)
    );

    vec3 ro = vec3(0.0);
    vec3 rd = normalize(vec3(uv, FOCAL));

    // The pixel footprint per unit of ray length. view() is height-normalized,
    // so one pixel is 2/height in uv units, and a uv offset of that at focal
    // length FOCAL opens by the same fraction of the distance marched. Read
    // off uResolution rather than hardcoded, so the style band-limits itself
    // on a tablet and on a 1080p phone alike.
    gEpsPerT = EPS_PIXELS * 2.0 / (max(uResolution.y, 1.0) * FOCAL);

    // Per-pixel start offset. The glow below is an integral along the ray, and
    // an integral sampled at the same depths on every pixel quantizes into
    // concentric shells that read as contour lines. hash13 rather than
    // fract(sin(dot(...))): the sin hash degenerates into bands at mediump,
    // which GLSL ES 3.00 permits and Mali does. Seeded on the pixel only, not
    // on time - a temporally varying dither on a fullscreen pass is flicker,
    // and this app has a photosensitivity budget.
    float jitter = hash13(vec3(gl_FragCoord.xy, 7.0));
    float t = 0.35 + jitter * 0.03;

    float hitT = -1.0;
    float hitTrapO = 1.0;
    float hitTrapA = 1.0;
    // The ray-integrated glow. Weight and weighted trap sums only: the colour
    // is resolved ONCE after the loop rather than by calling pal() per step -
    // pal() is a pair of cosines and, with a colour map selected, a texture
    // fetch, and 128 of those per pixel is the whole frame budget for a term
    // that is a soft halo.
    //
    // The traps are a weighted MEAN over the path, not the value at the ray's
    // closest approach. A single sample of a fractal orbit trap is as noisy as
    // the orbit is, and picking one point of it per ray put a different hue in
    // every pixel; a mean over the fifty-odd steps that carry any weight is
    // the same quantity with the noise integrated out.
    float glowSum = 0.0;
    float glowTrapO = 0.0;
    float glowTrapA = 0.0;

    for (int i = 0; i < MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        // Epsilon grows with the distance marched: a constant one shimmers on
        // the far side of the ball and wastes steps on the near side.
        float eps = gEpsPerT * t + EPS_FLOOR;
        if (d < eps) {
            hitT = t;
            hitTrapO = gTrapO;
            hitTrapA = gTrapA;
            break;
        }
        // Everything near a surface leaks light. No step cap is needed on this
        // quadrature: the weight is exp(-d * GLOW_TIGHT), so the long steps
        // are exactly the ones that contribute nothing, and the steps that do
        // contribute are short because they are near the geometry.
        float step = max(d * STEP_SCALE, eps * 0.5);
        float w = exp(-d * GLOW_TIGHT) * step;
        glowSum += w;
        glowTrapO += w * gTrapO;
        glowTrapA += w * gTrapA;
        t += step;
        if (t > FAR) break;
    }

    vec2 poleScreen = gPole.xy * FOCAL / max(gPole.z, 0.5);
    vec3 background = sky(rd, poleScreen);
    vec3 col;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        // Sampled slightly wider than the surface epsilon: the epsilon is
        // where the ray was allowed to stop, the normal is what the pixel is
        // shaded with, and the pixel is the wider of the two.
        float px = gEpsPerT * hitT + EPS_FLOOR;
        vec4 field = fieldAt(p, px * 1.3);
        vec3 n = field.xyz;
        // 1 where the four taps cancelled, i.e. where the surface under this
        // pixel is finer than the pixel and neither the normal nor the orbit
        // trap means anything. Everything below that would amplify that noise
        // - the specular, the trap highlight, the trap's own contribution to
        // the hue - is turned down by it, and the region is put in shadow,
        // which is also where it belongs: unresolved surface in this model is
        // the deepest, most enclosed part of it.
        //
        // It is a minority of the frame, and that is the point. At the lowest
        // Detail it fires on half a percent of pixels - the grazing edges - and
        // at the highest on one and a half, where the extra folds have put real
        // structure below the screen. A term that fired everywhere would be
        // flattening the style rather than protecting it.
        //
        // The band starts at 0.80 rather than at 1.0 because a clean surface
        // sampled across a strut's own curvature is already a little short of
        // linear; below 0.30 the taps are telling us nothing at all.
        float rough = 1.0 - smoothstep(0.30, 0.80, field.w);
        float ao = occlusion(p, n, clamp(0.03 + 0.012 * hitT, 0.03, 0.09));
        ao *= 1.0 - 0.55 * rough;

        // The orbit trap, as a palette coordinate. Two traps rather than one:
        // the distance to the ORIGIN bands the surface into the nested shells
        // the group generates, and the distance to the AXIS cuts across those
        // shells, so two surfaces that happen to sit at the same depth in the
        // group are still told apart. One trap alone gave concentric rings
        // that read as a target rather than as a structure.
        float trapO = sqrt(hitTrapO);
        float trapA = sqrt(hitTrapA);
        float hue = 0.10 + trapO * 0.62 + trapA * 0.30;
        // Where the surface is finer than the screen, the traps are as noisy
        // as the normal was - two neighbouring pixels stop on two unrelated
        // fragments of the group and pick two unrelated hues, which is the
        // confetti in colour form. Fade toward a hue that depends only on
        // where the point is in the ball, which is smooth by construction:
        // the region keeps a colour that belongs to it and loses the per-pixel
        // scatter. Not toward a constant - a flat patch reads as a hole.
        hue = mix(hue, 0.10 + 0.62 * clamp(length(p - gCenter) / BALL_R, 0.0, 1.0),
            rough * 0.9);

        vec3 body = pal(hue);
        vec3 rim = pal(hue + 0.34);

        vec3 key = normalize(vec3(0.48, 0.72, -0.50));
        vec3 fill = normalize(vec3(-0.62, -0.18, 0.76));
        float dif = clamp(dot(n, key), 0.0, 1.0);
        float bounce = clamp(dot(n, fill), 0.0, 1.0);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);

        // The ambient term is a constant, not an audio term: silence has to
        // land on a lit structure, not on a black screen. 0.14 is what it
        // takes for the inside of an arch - a surface facing neither light -
        // to still show which way it curves.
        col = body * (0.14 + 0.62 * dif + 0.26 * bounce) * ao;

        // The orbit-trap highlight - the filament along the surfaces whose
        // orbit hugged the axis. Treble sharpens it, which is the convention
        // (treble -> edge sharpness, fine detail, sparkle): at silence it is a
        // broad sheen over the arcs and at full treble it collapses onto the
        // ridges, so the same surface picks out its own edges as the top end
        // comes up. Exponent, not amplitude: sharpening an existing highlight
        // moves no area, where raising its amplitude would.
        float spark = pow(clamp(1.0 - trapA * 1.6, 0.0, 1.0), mix(5.0, 22.0, trebN));
        col += pal(hue + 0.5) * spark * (0.22 + 0.55 * trebN) * (1.0 - rough);

        // The rim: light gathering along the silhouette, which is where an
        // inverted lattice is most obviously not a Euclidean one - the
        // silhouette is made of arcs.
        col += rim * fres * (0.30 + 0.45 * energyN);
        col += vec3(1.0) * pow(clamp(dot(n, normalize(key - rd)), 0.0, 1.0),
            mix(24.0, 60.0, trebN)) * 0.30 * ao * (1.0 - rough);

        // Into the void with distance, so the far side of the ball sits behind
        // the near side rather than beside it.
        col = mix(col, background, 1.0 - exp(-hitT * FOG_RATE));
        // Dissolve the clip. The bounding ball is a hard intersection, so
        // without this the silhouette is a perfect circle and the whole style
        // reads as a disc with a picture on it rather than as a body with a
        // shape. Fading the last third of a unit of it into the void turns the
        // cut into the structure thinning out, which is what the construction
        // is doing there anyway - and it costs one length(), not a map().
        col = mix(background, col,
            smoothstep(0.0, 0.34, BALL_R - length(p - gCenter)));
    } else {
        col = background;
    }

    // The glow, coloured once. uEnergy is the density control by convention;
    // the floor keeps it present in silence, where it is the only thing giving
    // the gaps between the struts any depth.
    //
    // Put through 1 - exp(-x) rather than added raw. The raw sum is an
    // unbounded path integral through a fractal, so its top end is a handful
    // of outlier rays that spike far above their neighbours - and a spike in a
    // fullscreen additive term is exactly the shape of thing the flash budget
    // exists to keep out, quite apart from looking like dirt on the lens.
    float norm = max(glowSum, 1e-6);
    float glowHue = 0.10 + sqrt(glowTrapO / norm) * 0.62 + sqrt(glowTrapA / norm) * 0.30;
    col += pal(glowHue + 0.18) * (1.0 - exp(-glowSum * 1.1)) * (0.18 + 0.34 * energyN);

    // The wake: a soft lift under every finger, live and still fading.
    // touchWake() is unbounded above by design (five fingers sum to five), so
    // it goes through 1 - exp(-x) before it is allowed near the frame - five
    // fingers brighten one spot a little more than one finger does, and never
    // by five times. Local and gradual, which is what keeps it out of the
    // flash budget. Exactly zero when nothing is touching.
    float wake = 1.0 - exp(-touchWake(uv));
    col += pal(0.30 + 0.25 * energyN) * wake * 0.16;

    // Three additive layers, so this is HDR by construction. Clipping would
    // flatten every rim and every filament into the same white; the tone map
    // keeps them apart and, more to the point, bounds the whole frame's
    // luminance whatever the music does.
    col = vec3(1.0) - exp(-max(col, vec3(0.0)) * EXPOSURE);

    // Vignette in SCREEN space, off vUv rather than off view()'s uv. Off uv it
    // would be a function of the Zoom control: zoomed out far enough, dot(uv,
    // uv) is large everywhere and the vignette would take the whole frame to
    // black.
    vec2 nd = vUv * 2.0 - 1.0;
    col *= 1.0 - 0.20 * dot(nd, nd) * 0.5;

    fragColor = vec4(grade(col), 1.0);
}
