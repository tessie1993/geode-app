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

// MORPHOGEN - one organism, continuously metamorphosing between skeletons.
//
// There is exactly one body in this scene and it is never the same shape for
// long. Its distance field is a linear blend of four estimators that describe
// completely different solids - a woven minimal surface, a folded/inverted
// solid, a cluster of fused blobs and a hard faceted crystal - and the blend
// runs on the DISTANCES rather than on any geometry:
//
//     d = mix(dA, dB, t)
//
// That one line is the reason this style exists. A blendshape can only
// interpolate vertices, so it can never change topology: a sphere cannot
// become a torus without tearing, and a solid cannot become a woven sheet at
// all. A convex combination of two signed distance fields has no such
// restriction - the zero set of the blend passes through every intermediate
// topology on its own, opening holes and closing them wherever the two fields
// cross. The intermediate states are not poses of either skeleton; they are
// shapes neither one contains, and they are what the eye reads as growth.
//
// The blend also stays MARCHABLE for free. Both inputs never overestimate the
// distance to their own surface, and a convex combination of two functions
// that are each 1-Lipschitz is 1-Lipschitz, so the blended field is as safe to
// step as either end of it. Nothing else in this file is allowed to break that
// without paying for it: every non-rigid deform below states its Jacobian
// bound and gLip divides it back out, exactly as hyperspace_frag does with
// uLipschitz.
//
// ART DIRECTION, in one sentence: the seam is the subject. Where the two
// skeletons disagree, the surface belongs to neither one - it is the frontier
// of the transformation - and that is the part of the body that glows. When
// the morph settles onto a single skeleton the glow goes out, so the frame
// tells you at a glance whether the organism is resting or changing.

// ---- framing --------------------------------------------------------------

/**
 * Camera distance AND focal length, in world units.
 *
 * One number for both because that makes the ray through screen point uv cross
 * the body's centre plane (z = 0) at exactly uv * gViewScale - so a finger
 * lands on the piece of the body it is over, and the camera frame is the one
 * lib_touch's helpers document (x,y = the screen axes, z along the view).
 * Larger flattens the perspective; below about 2.5 the near lobes of the body
 * fish-eye badly enough that a metamorphosis reads as a zoom.
 */
#define CAM_DIST 3.4

/**
 * World half-extent guaranteed visible along the SHORT screen axis.
 *
 * view() is height-normalized - uv spans [-aspect, aspect] x [-1, 1] - which
 * is right for a field that fills the plane and wrong for a single centred
 * subject: on a 9:20 phone held upright the horizontal half-extent is 0.45,
 * so a body of radius 1 would have had two thirds of its width cropped off.
 * Dividing the ray fan by min(aspect, 1) fits the body to whichever axis is
 * shorter instead, and 1.12 against a body radius around 1.0 leaves the
 * silhouette just kissing the frame rather than floating in the middle of it.
 */
#define FRAME_R 1.12

/** The radius the four skeletons are built and clipped to. */
#define BODY_R 1.00

/**
 * Radius outside which no skeleton is evaluated at all.
 *
 * It only has to contain the BODY - the touch bulges are measured exactly and
 * separately, which is what keeps this radius tight, and a tight radius is the
 * whole saving: outside it a march step costs one length() instead of two
 * distance estimators.
 *
 * Sizing it means knowing how far a smooth union can push a surface OUT, and
 * that is k, not k/4: smin subtracts h*h*(4k)*0.25 and h reaches 1 on the
 * seam. Getting that wrong by a factor of four is what turned the metaball
 * cluster into a single egg the size of the frame the first time this ran.
 * Worst case here is the metaball cluster: reach 0.60 plus the 0.09 wobble
 * plus the largest lobe radius 0.34 gives 1.03, one fuse can add 0.11 (four
 * chained fuses cannot all peak at one point - that would need five spheres
 * equidistant there, and five spheres equidistant at a point all contain it),
 * plus 0.009 of relief and 0.030 of shockwave = 1.18. The rest is the margin
 * that makes the clip below invisible rather than a straight edge across the
 * silhouette.
 */
#define BODY_BOUND 1.28

/**
 * Radius of the sphere the analytic ray cull and the final clip use, once a
 * finger is down: BODY_BOUND fused with a full-strength bulge.
 *
 * Untouched frames use BODY_BOUND instead, so nothing about the cull changes
 * until someone actually touches the screen.
 */
#define TOUCH_BOUND 1.36

/**
 * How far outside BODY_BOUND map() stops bothering with the body.
 *
 * A gate at exactly zero makes the march creep: the step lands on the sphere,
 * the returned bound is zero, and the ray never gets inside. Same reasoning as
 * hyperspace_frag's uBoundMargin.
 */
#define BODY_MARGIN 0.06

/** MarchBudget.MAX_STEPS. The loop bound; uSteps is what the body breaks on. */
#define MARCH_MAX 128

/**
 * Fraction of the distance estimate actually stepped.
 *
 * Not 1.0, because two things in this field are estimates rather than exact
 * distances: the box-fold's analytic DE (conservative in the open, optimistic
 * in the thin sheets between folds) and smax's blend, which can overestimate
 * inside a subtracted cavity - lib_sdf3 says so at smax. The fold solid is
 * what sets it: at 0.72 its crust speckled with missed hits where the sheets
 * get thinner than a step, which reads as dirt on the surface rather than as
 * an overshoot, so it is easy to mistake for texture.
 */
#define STEP_SCALE 0.68

/**
 * Surface threshold as a fraction of distance travelled.
 *
 * Proportional rather than constant: a constant epsilon shimmers on the far
 * side of the body and wastes a third of the budget on the near side. The
 * floor keeps it above float noise when the camera is very close.
 */
#define HIT_EPS 0.0016

// ---- the morph clock ------------------------------------------------------

/**
 * Skeleton changes per second: one every 6.25 s, four skeletons, so the
 * organism works through its whole repertoire in 25 s.
 *
 * ShaderScene wraps uTime at 7100 s and 7100 * 0.16 = 1136, a whole multiple
 * of the four-target cycle, so the two-hourly wrap lands the clock back on the
 * same skeleton instead of cutting a metamorphosis in half. (Float rounding
 * moves that by a fraction of one segment at the wrap instant; a single early
 * swap once every two hours is not worth a modulo in the hot path.)
 */
#define MORPH_RATE 0.16

/**
 * Longest fraction of a segment spent HOLDING a finished skeleton.
 *
 * Without a hold the body is permanently between two shapes and never reads as
 * being anything; with one it arrives, is briefly legible, and then leaves.
 * The user's Morph control shortens it to zero, which is the honest meaning of
 * that slider on the style named after morphing - view() has already spent it
 * once on its polar remap, and one slider meaning "more transformation" in two
 * places on the same style is not a conflict.
 */
#define MORPH_HOLD 0.44

/**
 * How far a beat lunges the body toward its next skeleton.
 *
 * This is the discrete beat event, and it is deliberately the ONLY thing the
 * beat moves in the geometry. The segment index cannot be beat-derived - a
 * fragment shader has no memory and cannot count beats, only read the phase of
 * the current one - so the clock owns the CADENCE and the beat owns the
 * MOTION: on every hit the organism surges toward what it is becoming and
 * relaxes back, the surges grow as the segment runs out because they are
 * scaled by (1 - t), and the last one before the ease saturates is what
 * completes the change. So the arrival is always on a beat even though the
 * schedule is not.
 */
#define MORPH_KICK 0.42

/** Below this the second skeleton is not evaluated at all - see map(). */
#define MORPH_EPS 0.004

/**
 * Width of the seam line, in world units of disagreement between the two
 * skeletons.
 *
 * The seam is where the two skeletons AGREE - the curve along which their two
 * surfaces intersect. Every blend passes exactly through it, whatever t is,
 * because dA = dB = 0 makes mix(dA, dB, t) = 0 for all t: it is the hinge the
 * whole metamorphosis pivots around, fixed while everything either side of it
 * moves. On one side of that curve the blended surface is following the old
 * skeleton and on the other it is already following the new one, which is
 * exactly the "what it was / what it is becoming" boundary this style is
 * about.
 *
 * Deliberately narrow, and narrower than it first looks it should be. The
 * separation |dA - dB| on the blended surface is bounded by whichever of the
 * two fields saturates first, and the gyroid's tops out around 0.17 - so at
 * 0.03 the "seam" covered most of every gyroid blend as a soft wash rather
 * than marking anything. 0.014 is about three hit epsilons at this camera
 * distance, i.e. a two- or three-pixel line at 1080p: wide enough not to
 * alias, narrow enough that it is a LINE drawn across the body rather than a
 * brightness change over a large area - which is the thing the flash budget
 * exists to prevent. A line can be as hot as it likes.
 */
#define FRONT_WIDTH 0.014

// ---- skeleton 0: the gyroid ----------------------------------------------

/**
 * Gyroid cells per world unit, and a cost control as much as a look.
 *
 * The estimate below is (|g| - thickness) / (FREQ * LIP), so doubling the
 * frequency halves every step a ray takes near the weave. At 8 the cell is
 * 0.785 world units, about two and a half periods across the body, which is
 * enough to read as woven rather than as two folded sheets - and it costs
 * roughly six march steps to cross one gap between sheets.
 */
#define GYROID_FREQ 8.0

/**
 * The supremum of |grad(sin(q).cos(q.yzx))|, and the reason the sheet is
 * marchable at all.
 *
 * Each partial is cos(x)cos(y) - sin(x)sin(z), which alone is bounded by 2,
 * but the three cannot reach 2 together: numerically maximized over the cell
 * the norm tops out at exactly sqrt(3), attained wherever every partial is
 * +-1 (the origin, and the cell corners). Using the loose 2*sqrt(3) instead
 * would have halved every step near the weave for nothing. Using anything
 * BELOW sqrt(3) is what fills the sheets with camera-locked holes, so this
 * number is a ceiling and not a taste control.
 */
#define GYROID_LIP 1.7320508

/** How far the weave crawls through itself per second, in gyroid units. */
#define GYROID_CRAWL 0.33

// ---- skeleton 1: the box-fold / inversion solid ---------------------------

/** Compile-time ceiling on the fold loop. Runtime count follows uSteps. */
#define FOLD_MAX_ITERS 6

/** World units per fold unit: the interesting structure lives inside |c| ~ 3. */
#define FOLD_ZOOM 3.0

/** Inner radius of the sphere inversion, squared. */
#define FOLD_MIN2 0.25

/** Fixed radius of the sphere inversion, squared. */
#define FOLD_FIX2 1.00

/**
 * How much the fold solid is inflated, in world units.
 *
 * A Mandelbox at these scales is a forest of spikes whose thinnest parts are
 * well under a pixel at any resolution a phone has. Rendered raw it is not
 * detail, it is NOISE: every frame the sub-pixel spikes are sampled somewhere
 * else, the surface normals are random, and the skeleton reads as dark speckle
 * that crawls. opRound is a constant offset of the field - exact, Lipschitz-1,
 * free - so inflating by about three pixels at the body's silhouette welds
 * the sub-pixel structure into solid relief that the light can actually
 * describe, and leaves everything coarser than that untouched.
 */
#define FOLD_ROUND 0.018

// ---- skeleton 2: the metaball cluster ------------------------------------

/** Reach of the lobe centres from the core, at full extension. Far enough out
 *  that the lobes read as lobes rather than as bumps, close enough that the
 *  core always swallows their inner halves. */
#define BLOB_ORBIT 0.60

/** Lateral drift of a lobe off its own axis. Small: enough that the star is
 *  never symmetric, not enough to let two lobes trade places. */
#define BLOB_WOBBLE 0.09

/** Radius of the core the four lobes hang off.
 *
 *  Without it the cluster was four spheres that drifted in and out of contact:
 *  when three happened to line up it was a featureless peanut, and when they
 *  spread it came apart into separate balls. Neither is one organism. A core
 *  guarantees the cluster is a single connected thing whose lobes move,
 *  which is what the other three skeletons are and what the morph needs at
 *  both ends of a blend. */
#define BLOB_CORE 0.32

/** Orbits per second. Slow enough that the cluster drifts rather than spins. */
#define BLOB_RATE 0.21

/**
 * Fusion radius between blobs.
 *
 * Wide enough that the parts are ONE substance - the necks between lobes are
 * drawn by the blend, not by either sphere - and no wider, because smin pushes
 * the surface out by k and three chained fuses at 0.28 inflated the cluster
 * into a featureless egg with no lobes left to see.
 */
#define BLOB_K 0.11

// ---- skeleton 3: the crystal ---------------------------------------------

/** Centre-to-vertex radius of the octahedron. */
#define CRYSTAL_R 0.88

/** Ring radius of the two armillary tori; outside the octahedron's faces
 *  (which sit at R/sqrt(3) = 0.51) so they read as bands around it. */
#define CRYSTAL_RING 0.80

/** Tube radius. Thin: this skeleton is the hard one. */
#define CRYSTAL_TUBE 0.075

/**
 * Fusion radius inside the crystal.
 *
 * Less than half the blob radius, and that contrast is the job: the organic
 * states only read as organic because this one does not. At 0.10 the fuse
 * rounded the octahedron's eight edges into a pillow and swallowed the rings
 * whole - the crystal segment was indistinguishable from the blob segment.
 */
#define CRYSTAL_K 0.05

/** Turns per second of the crystal's own spin. */
#define CRYSTAL_SPIN 0.17

// ---- skin, shockwave, touch ----------------------------------------------

/** Spatial frequency of the bass relief. */
#define RELIEF_FREQ 9.0

/**
 * Peak relief amplitude, in world units, at full bass.
 *
 * Tiny on purpose. It is a SKIN, not a shape: the four skeletons are the
 * shape, and relief deep enough to see across the room also multiplies the
 * march cost through RELIEF_LIP below. 0.009 is about a pixel of displacement
 * at the body's silhouette, which is exactly enough to make the surface look
 * like it is under pressure.
 */
#define RELIEF_AMP 0.009

/** RELIEF_FREQ * sqrt(3): the gradient bound of a product of three sines. */
#define RELIEF_LIP 15.588

/** Peak geometric amplitude of the beat shockwave, in world units. */
#define SHELL_AMP 0.030

/** Half-width of the shockwave front. Wider = cheaper Lipschitz, softer ring. */
#define SHELL_W 0.16

/** max over u of (2u/W)exp(-u^2) = 0.8578/W: the front's steepest slope. */
#define SHELL_LIP 5.361

/**
 * Where a touch bulge is centred, as a radius from the body's centre.
 *
 * Two failure modes bracket this number and they pull in opposite directions.
 * Seated too far in (0.6 and below) the bulge is entirely inside the body on
 * every skeleton and a finger does nothing visible at all. Seated too far out
 * it detaches from the hard skeletons - the octahedron's faces are only 0.51
 * from the centre - and a finger grows a ball floating in space instead of
 * the body reaching for it. 0.85 with a 0.12 fusion radius protrudes on the
 * round skeletons and still bridges to the crystal's faces across a blend
 * window a quarter of the body wide.
 *
 * It is also, at this camera, the number that decides how big a finger on the
 * middle of the screen looks: the bulge points straight at the viewer there,
 * so it is magnified by CAM_DIST/(CAM_DIST - reach). At 0.98 that was 1.4x and
 * a single centred touch filled a third of the frame with one flat unlit cap.
 */
#define BULGE_SEAT 0.85

/** Radius of a bulge at full strength; scales with the point's decaying .z so
 *  a lifted finger leaves a swelling that subsides instead of vanishing. */
#define BULGE_R 0.24

/**
 * Fusion radius of a bulge into the body.
 *
 * Wide enough that the neck between body and bulge is drawn by the BLEND
 * rather than by either shape - that neck is what makes the body look like it
 * is reaching - and no wider, because smin's blend window is 4k, not k. At
 * 0.26 that window was 1.04 world units, wider than the body itself, so one
 * finger did not grow a bulge: it inflated the whole organism into a
 * featureless sphere that overflowed the frame. Every smin radius in this file
 * is small for the same reason - the WINDOW is what has to stay local.
 */
#define BULGE_K 0.12

/** How far in front of the body the finger direction is taken to be. Smaller
 *  splays the bulges toward the silhouette; larger points them at the camera. */
#define BULGE_DEPTH 0.75

/** Sentinel for "no bulge anywhere near". Any value far enough outside the
 *  bounding sphere that smin's blend window cannot reach it, so an untouched
 *  frame gets smin(d, TOUCH_FAR, k) == d EXACTLY, not approximately. */
#define TOUCH_FAR 1e4

/** uTouchSpin is clamped to +-8 rad/s by TouchField.MAX_SPEED; this is the
 *  gain onto the vortex angle, held so the deform's Lipschitz bound
 *  sqrt(1 + k^2) stays under 1.3 even when someone spins as fast as the
 *  digitizer will report. */
#define SPIN_GAIN 0.10

// ---- what map() hands back ------------------------------------------------
//
// GLSL ES has no out-params worth the register traffic on a function called up
// to 134 times per pixel, so the surface's material comes back through
// globals, the way hyperspace_frag returns gTrap/gHue/gGlow. They are valid
// only for the LAST map() call, which is why main() captures them the instant
// the march breaks and before the normal taps overwrite them.

/** Seam heat: 1 where a smooth union is exactly half one part and half the
 *  other, 0 deep inside either. Set by fuse() below. */
float gFuse;

/** Frontier heat: 1 where the two skeletons disagree most, and 0 whenever the
 *  morph has settled onto one of them. */
float gFront;

// ---- per-frame values, computed once in main() ---------------------------

float gAspect;
float gViewScale;
float gMt;
float gIa;
float gIb;
int gFoldIters;
float gFoldScale;
float gWeaveThick;
float gWeaveT;
float gSwell;
vec3 gBlobC[4];
float gBlobRad[4];
mat3 gCrystalRot;
float gRelief;
float gShellA;
float gShellR;
float gSpinK;
float gSceneBound;
float gLip;
mat3 gSpin;
vec3 gBulgeC[TOUCH_MAX_POINTS];
float gBulgeR[TOUCH_MAX_POINTS];
int gBulgeN;

/**
 * Smooth union that also reports its own seam.
 *
 * sminMat's .y is 0 deep inside a, 1 deep inside b and 0.5 on the join, so
 * 4m(1-m) is a hat peaking exactly on the join - the locus where the surface
 * is made of both parts and belongs to neither. Accumulated with max() rather
 * than added: a point that is on two seams at once is not twice as hot, it is
 * on a seam.
 */
float fuse(float a, float b, float k) {
    vec2 m = sminMat(a, b, k);
    gFuse = max(gFuse, 4.0 * m.y * (1.0 - m.y));
    return m.x;
}

// ---- the four skeletons ---------------------------------------------------

/**
 * A gyroid: the triply periodic minimal surface sin(x)cos(y) + sin(y)cos(z) +
 * sin(z)cos(x) = 0, thickened into a sheet and clipped to a ball.
 *
 * Almost free - three sines, three cosines and a dot - and it is the only one
 * of the four with no inside and no outside, so the blend into it is where the
 * body opens up and becomes something light can pass through.
 *
 * The translation is what keeps it alive in silence: the surface is periodic,
 * so sliding the domain along y is a rigid motion of an infinite object and
 * the weave appears to crawl through itself forever without ever repeating a
 * silhouette.
 */
float tGyroid(vec3 p) {
    vec3 q = p * GYROID_FREQ;
    q.y += gWeaveT;
    float g = dot(sin(q), cos(q.yzx));
    float sheet = (abs(g) - gWeaveThick) / (GYROID_FREQ * GYROID_LIP);
    // Intersected with a ball rather than min()'d with one: the gyroid is
    // endless, and a union would have let it streak out of frame in every
    // direction as an infinite lattice instead of being a body.
    return smax(sheet, length(p) - BODY_R, 0.12);
}

/**
 * The box-fold / inversion solid: Mandelbox-style.
 *
 * Each iteration reflects any coordinate that has escaped the unit box back
 * across it, inverts anything inside a small ball out to a fixed radius, then
 * scales and re-adds the original point. Both folds are piecewise isometries,
 * so the running derivative dr is exact for them; only the scale multiplies
 * it, which is why length(z)/dr is a usable estimate after a handful of steps.
 *
 * The sphere fold is written branchless. The textbook form is two nested ifs
 * on r2, and the three cases collapse to one clamp because
 * FIX2/clamp(r2, MIN2, FIX2) is FIX2/MIN2 below the inner radius, FIX2/r2
 * between them and exactly 1 outside - and a clamp cannot divide by zero at
 * the origin the way the raw ratio can.
 */
float tFold(vec3 p) {
    vec3 c = p * FOLD_ZOOM;
    vec3 z = c;
    float dr = 1.0;
    for (int i = 0; i < FOLD_MAX_ITERS; i++) {
        if (i >= gFoldIters) break;
        z = clamp(z, -1.0, 1.0) * 2.0 - z;
        float f = FOLD_FIX2 / clamp(dot(z, z), FOLD_MIN2, FOLD_FIX2);
        z *= f;
        dr *= f;
        z = z * gFoldScale + c;
        dr = dr * abs(gFoldScale) + 1.0;
    }
    float d = opRound(length(z) / (abs(dr) * FOLD_ZOOM), FOLD_ROUND);
    // Hard max, not smax: the escaped arms of a Mandelbox are unbounded, and a
    // soft clip would have rounded the one edge that tells you this skeleton
    // is machined rather than grown.
    return max(d, length(p) - BODY_R);
}

/**
 * Four spheres on independent orbits, fused into one substance.
 *
 * The rates are mutually irrational-looking on purpose. On round ratios the
 * four blobs return to the same relative positions every few seconds and the
 * cluster reads as a rigid object rotating; on these it never repeats inside
 * the segment it gets, so it reads as something being stirred.
 *
 * smin is not associative (lib_sdf3 says so at length), so this fuse order is
 * part of the shape: blob 0 is the trunk every other blob joins, which is why
 * it is the largest.
 */
float tBlobs(vec3 p) {
    float d = sdSphere(p, BLOB_CORE * gSwell);
    d = fuse(d, sdSphere(p - gBlobC[0], gBlobRad[0]), BLOB_K);
    d = fuse(d, sdSphere(p - gBlobC[1], gBlobRad[1]), BLOB_K);
    d = fuse(d, sdSphere(p - gBlobC[2], gBlobRad[2]), BLOB_K);
    d = fuse(d, sdSphere(p - gBlobC[3], gBlobRad[3]), BLOB_K);
    return d;
}

/**
 * The crystal: an octahedron with two armillary rings.
 *
 * lib_sdf3's octahedron is the exact Voronoi-region form rather than the
 * plane form, which matters more here than anywhere else in the file - this is
 * the skeleton whose vertices the camera grazes, and the cheap estimator
 * overestimates exactly there.
 *
 * Its own spin is slower than the body's, so during the crystal segment the
 * two rotations beat against each other and the facets keep catching the key
 * light at new angles instead of turning as one rigid lump.
 */
float tCrystal(vec3 p) {
    vec3 q = gCrystalRot * p;
    float d = sdOctahedron(q, CRYSTAL_R * gSwell);
    d = fuse(d, sdTorus(q, vec2(CRYSTAL_RING, CRYSTAL_TUBE)), CRYSTAL_K);
    // The second ring is the first one about the +x axis instead of +y, and a
    // swizzle is the whole rotation: sdTorus's axis is its argument's y, so
    // handing it q.yxz puts the ring in the perpendicular plane for free. (The
    // swizzle is a reflection as well as a rotation, and a torus does not care.)
    d = fuse(d, sdTorus(q.yxz, vec2(CRYSTAL_RING * 0.86, CRYSTAL_TUBE * 0.8)), CRYSTAL_K);
    return d;
}

/** Dispatch. The index is a uniform-derived value, so every lane in the quad
 *  takes the same branch and this costs a jump, not a divergence. */
float targetDE(float idx, vec3 p) {
    if (idx < 0.5) return tGyroid(p);
    if (idx < 1.5) return tFold(p);
    if (idx < 2.5) return tBlobs(p);
    return tCrystal(p);
}

/**
 * Distance to the nearest touch bulge, or TOUCH_FAR when nothing is touching.
 *
 * The centres and radii were built once per frame in main(); doing the
 * normalize() here instead cost five square roots per march step for a value
 * that cannot change within a frame.
 */
float bulgeDist(vec3 p) {
    float d = TOUCH_FAR;
    for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
        if (i >= gBulgeN) break;
        d = min(d, length(p - gBulgeC[i]) - gBulgeR[i]);
    }
    return d;
}

/**
 * The whole organism.
 *
 * Order matters and is not arbitrary: the vortex and the body's own spin are
 * both isometries, so they can be applied to p before anything else without
 * invalidating the bounding sphere (neither changes |p|); the touch bulges are
 * fused LAST, in the camera frame, because a bulge belongs to the finger and
 * must not be dragged around by the body's rotation.
 */
float map(vec3 p) {
    gFuse = 0.0;
    gFront = 0.0;

    // One length(), used three times: the body's bound, the shockwave's radius
    // and the final clip.
    float r = length(p);

    // Every skeleton is inside BODY_BOUND, so the distance to that sphere is
    // already a valid lower bound on the distance to the body and the ray can
    // step by it without any skeleton being evaluated. This is what makes the
    // style affordable: a ray that only clips the silhouette pays one length(),
    // not two distance estimators.
    //
    // The bulges are NOT folded into that radius - they are measured exactly,
    // and cheaply, because they are spheres. Inflating the radius to cover a
    // finger at the edge of the screen would have meant paying for the body
    // everywhere the finger could reach.
    float bound = r - BODY_BOUND;
    float db = bulgeDist(p);
    if (bound > BODY_MARGIN) return smin(bound, db, BULGE_K);

    vec3 q = p;
    // Three or more fingers wring the body about the view axis. The angle
    // falls off linearly to zero at the bounding radius, which is what bounds
    // the deform: for p -> rot(theta(r)) p the Jacobian norm is
    // sqrt(1 + (r theta'(r))^2), and with theta' = -k/BODY_BOUND the r cancels
    // and the bound is sqrt(1 + k^2) everywhere. gLip carries it.
    //
    // uTouchSpin is a RATE, not an angle, and there is nowhere in a fragment
    // shader to integrate it - so it is read as an amount of twist instead:
    // swirl faster and the body is wrung harder, stop and it unwinds. Its
    // 0.25 s smoothing in TouchField is what keeps that continuous.
    if (gSpinK != 0.0) {
        float ang = gSpinK * (1.0 - clamp(length(q.xy) / BODY_BOUND, 0.0, 1.0));
        q.xy = rot2(ang) * q.xy;
    }
    q = gSpin * q;

    // THE MORPH. Both skeletons are only evaluated while the body is actually
    // between them; during a hold - which is most of a segment at the default
    // Morph setting - this halves the cost of every march step.
    float d;
    if (gMt <= MORPH_EPS) {
        d = targetDE(gIa, q);
    } else if (gMt >= 1.0 - MORPH_EPS) {
        d = targetDE(gIb, q);
    } else {
        float dA = targetDE(gIa, q);
        float dB = targetDE(gIb, q);
        d = mix(dA, dB, gMt);
        // The seam, and it has to be scale-free. The four skeletons do not
        // share a distance scale - the gyroid's estimate is divided by
        // FREQ * LIP and is an order of magnitude smaller than the crystal's -
        // so a threshold in world units answered "these two agree everywhere"
        // for every blend the gyroid was in. A ratio does not care: it is 1
        // where the two surfaces cross and falls to 0 over FRONT_WIDTH either
        // side, whatever units the fields are in.
        //
        // 4t(1-t) puts it out entirely once the body has settled onto one
        // skeleton, because then there is nothing being negotiated.
        float sep = abs(dA - dB);
        gFront = (FRONT_WIDTH / (sep + FRONT_WIDTH)) * (4.0 * gMt * (1.0 - gMt));
    }

    // Bass relief: a shallow triple-sine skin over whatever the body currently
    // is, so pressure shows on all four skeletons instead of one of them
    // owning the bass. Amplitude is proportional to bass, so at silence this
    // subtracts exactly zero and gLip's factor is exactly one.
    if (gRelief > 0.0) {
        d -= gRelief * sin(q.x * RELIEF_FREQ)
            * sin(q.y * RELIEF_FREQ + 1.7)
            * sin(q.z * RELIEF_FREQ + 3.1);
    }

    // The beat's discrete event, as a shockwave rather than a new object: a
    // Gaussian ridge at radius gShellR, born at the transient deep inside the
    // body and expanding out through the skin as uBeatPhase runs. Born inside
    // means it is invisible at the moment it appears, which is the difference
    // between a pulse crossing the surface and a sphere popping into frame.
    if (gShellA > 0.0) {
        float u = (r - gShellR) / SHELL_W;
        d -= gShellA * exp(-u * u);
    }

    // The fingers, fused into whatever the body is. With nothing touching this
    // is smin(d, 1e4, k), whose blend window cannot reach across 1e4, so it
    // returns d bit-for-bit.
    d = fuse(d, db, BULGE_K);

    // Clipped to the cull sphere BY CONSTRUCTION rather than by an argument
    // about how far each blend can push its surface. Those arguments compound
    // - four fuses and a bulge on top of them - and the moment one of them is
    // off, rays get killed in front of real geometry and the silhouette grows
    // a straight edge nobody can explain. An intersection with the sphere the
    // ray was culled against cannot be wrong: it makes the cull's premise true.
    return max(d / gLip, r - gSceneBound);
}

/** Tetrahedral normal: four map() taps instead of the six a central
 *  difference needs, and symmetric, so a flat facet reads flat. */
vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(
        k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e) +
            k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e)
    );
}

/**
 * Two-tap ambient occlusion. Each tap is a whole map() - the most expensive
 * lighting term in the file - so it is two and not five, and the 1.7 factor
 * renormalizes the short sum back to about the occlusion depth a longer one
 * gives. Deliberately a little short of it: the fold skeleton's crevices are
 * dense enough that a faithful two-tap occlusion buried the whole body, and
 * the travelled-steps term below is already occluding the same geometry.
 */
float occlusion(vec3 p, vec3 n, float scale) {
    float occ = 0.0;
    float w = 1.0;
    for (int i = 1; i <= 2; i++) {
        float h = scale * float(i) * 0.5;
        occ += (h - map(p + n * h)) * w;
        w *= 0.55;
    }
    return clamp(1.0 - 1.7 * occ, 0.0, 1.0);
}

/**
 * The room the organism is in.
 *
 * Two fbm octaves on the ray DIRECTION, so it is infinitely far away, has no
 * edges and turns with the view. Kept between about 0.05 and 0.20 before the
 * grade: it has to be dark enough that the body owns the frame, and bright
 * enough that a silent screen is not black - a dead-black idle reads as a
 * crash, not as a visualizer waiting for music.
 *
 * Squaring the noise is what does that. A linear fbm fills the screen with
 * mid-grey cloud; the square leaves most of it near zero and lets a few
 * regions rise, which reads as depth instead of fog.
 */
vec3 room(vec3 rd, vec2 uv, float hue) {
    float f = fbm3(rd * 2.6 + vec3(0.0, uTime * 0.035, uTime * 0.021), 2);
    vec3 c = pal(hue + 0.62 + f * 0.18) * (0.050 + 0.150 * f * f);
    // The wake of every finger, live and still fading, as a glow in the room
    // behind the body. Clamped because touchWake is unbounded above by design
    // - five fingers in one place sum to five - and a full-screen brightness
    // term is exactly the kind of thing VisualSafety's flash budget is about.
    c += pal(hue + 0.30) * clamp(touchWake(uv), 0.0, 3.0) * 0.045;
    return c * (0.85 + 0.30 * clamp(uEnergy, 0.0, 1.5));
}

void main() {
    // view() first: zoom, rotation, drift, kaleidoscope, tiling, pixelate,
    // shake, twist, warp, ripple and the beat pulse all live in there, and a
    // style that builds its own uv is a style where fifteen Customize controls
    // silently stop working.
    vec2 uv = view();

    gAspect = uResolution.x / max(uResolution.y, 1.0);
    gViewScale = FRAME_R / max(min(gAspect, 1.0), 0.2);

    // ---- the clock ---------------------------------------------------------
    float clock = uTime * MORPH_RATE;
    float segf = floor(clock);
    float f = fract(clock);
    gIa = mod(segf, 4.0);
    gIb = mod(segf + 1.0, 4.0);

    float beatEnv = clamp(uBeat, 0.0, 1.0);
    float beatBump = pow(0.5 + 0.5 * cos(6.2831853 * uBeatPhase), 2.0);
    float kick = beatEnv * beatEnv * beatBump;

    float hold = MORPH_HOLD * (1.0 - clamp(uMorph, 0.0, 1.0));
    gMt = smoothstep(hold * 0.5, 1.0 - hold * 0.5, f);
    // The kick is tapered to nothing at both ends of the segment. Without the
    // taper a beat landing on the wrap took the body from "finished skeleton
    // B" straight to "40% of the way from B to C" in one frame, because at the
    // wrap B stops being the destination and starts being the origin. Tapered,
    // both sides of the wrap agree that the body is exactly B and nothing
    // jumps.
    float window = smoothstep(0.0, 0.15, f) * (1.0 - smoothstep(0.85, 1.0, f));
    gMt = clamp(gMt + MORPH_KICK * kick * window * (1.0 - gMt), 0.0, 1.0);

    // ---- audio, clamped before anything structural reads it ---------------
    //
    // These bands are auto-gained and per-frame. Nothing here is slew-limited
    // slew-limited on the Kotlin side, so they are allowed to move the
    // body by a few per cent and no more: what VisualSafety cannot clamp is
    // AREA, and a band value driving a size directly is how a quiet passage
    // and a loud one end up covering different fractions of the screen from
    // one frame to the next.
    float bass = clamp(uBass, 0.0, 1.2);
    float mid = clamp(uMid, 0.0, 1.2);
    float treb = clamp(uTreble, 0.0, 1.5);
    float energy = clamp(uEnergy, 0.0, 1.5);

    // Thicker weave under bass: the sheets swell toward each other and the
    // gyroid closes up, which is the one deformation this skeleton has.
    gWeaveThick = 0.30 + 0.42 * bass;
    gWeaveT = uTime * GYROID_CRAWL;
    gSwell = 1.0 + 0.10 * bass;

    // The blob cluster and the crystal's own spin, built ONCE. None of this
    // depends on the sample point, and map() runs up to 134 times per pixel:
    // twelve sines and two more inside the march was most of what the metaball
    // skeleton cost, and none of it was doing any work.
    //
    // The lobes point down the four TETRAHEDRAL directions and only their
    // lengths breathe. Four fully independent orbits were the obvious way to
    // write this and the wrong one: their positions are uncorrelated, so every
    // few seconds three of them lined up and the organism was a featureless
    // sausage, and a moment later they spread and it came apart. Fixed
    // directions make it a four-lobed star at EVERY instant - the four
    // directions also sum to zero, so the cluster cannot lean - while the
    // breathing lengths and the small lateral wobble keep it from reading as a
    // rigid object being rotated. The rates are mutually irrational-looking so
    // no two lobes ever fall into step.
    float bt = uTime * BLOB_RATE;
    const float TET = 0.5773503;
    gBlobC[0] = vec3(TET, TET, TET) * (BLOB_ORBIT * (0.82 + 0.18 * sin(bt)))
        + vec3(sin(bt * 0.83), cos(bt * 0.61), sin(bt * 1.13)) * BLOB_WOBBLE;
    gBlobC[1] = vec3(TET, -TET, -TET) * (BLOB_ORBIT * (0.82 + 0.18 * cos(bt * 0.71 + 2.1)))
        + vec3(cos(bt * 1.07), sin(bt * 0.47), cos(bt * 0.89)) * BLOB_WOBBLE;
    gBlobC[2] = vec3(-TET, TET, -TET) * (BLOB_ORBIT * (0.82 + 0.18 * sin(bt * 1.31 + 4.2)))
        + vec3(sin(bt * 0.59), cos(bt * 1.19), sin(bt * 0.73)) * BLOB_WOBBLE;
    gBlobC[3] = vec3(-TET, -TET, TET) * (BLOB_ORBIT * (0.82 + 0.18 * cos(bt * 0.43 + 5.0)))
        + vec3(cos(bt * 0.67), sin(bt * 0.97), cos(bt * 1.27)) * BLOB_WOBBLE;
    gBlobRad[0] = 0.34 * gSwell;
    gBlobRad[1] = 0.30 * gSwell;
    gBlobRad[2] = 0.28 * gSwell;
    gBlobRad[3] = 0.32 * gSwell;
    gCrystalRot = rotY(uTime * CRYSTAL_SPIN);
    // The box-fold's scale is the parameter its whole shape lives on, so it is
    // where the fold skeleton's slow life and its mid-band steering both go.
    //
    // Centred on -2 rather than the more usual -1.5: the MAGNITUDE sets how
    // chunky the solid is, and the fine end of that range is where it becomes
    // the sub-pixel spike forest FOLD_ROUND exists to rescue. Negative
    // throughout, which is the classic hollowed Mandelbox - a positive scale
    // gives a solid lump with the folds only on its skin. A tenth of a unit of
    // mids is a visible change of character and still well inside the band
    // where the estimator is well behaved.
    gFoldScale = -1.95 + 0.28 * sin(uTime * 0.037) + 0.10 * mid;
    // The fold loop is where the cost is, so it is what Detail buys: 3 folds
    // at uSteps 64, 4 at the default 102, 6 at 128. 6/128 is exact in binary,
    // so both ends of the Detail range land on whole numbers rather than on a
    // rounding.
    gFoldIters = int(clamp(floor(uSteps * 0.046875), 3.0, float(FOLD_MAX_ITERS)));
    gRelief = RELIEF_AMP * bass;

    // The shockwave rides uBeatPhase from inside the body to just past the
    // skin, and its amplitude rides the squared envelope so it is silent
    // between hits and absent altogether in silence.
    gShellA = SHELL_AMP * beatEnv * beatEnv;
    gShellR = mix(0.10, 1.16, clamp(uBeatPhase, 0.0, 1.0));

    gSpinK = SPIN_GAIN * clamp(uTouchSpin, -8.0, 8.0);

    // The organism's own slow turn, on two unrelated rates so it never returns
    // to the same attitude. This is the motion that has to hold the screen
    // when there is no music at all.
    gSpin = rotY(uTime * 0.058) * rotX(0.30 * sin(uTime * 0.041));

    // Every non-rigid deform in map() multiplied together. Each factor is
    // exactly 1 when its driver is 0, so a silent untouched frame divides by
    // exactly 1.0 and marches at full speed.
    gLip = sqrt(1.0 + gSpinK * gSpinK)
        * (1.0 + RELIEF_LIP * gRelief)
        * (1.0 + SHELL_LIP * gShellA);

    // ---- where the fingers are, as geometry -------------------------------
    //
    // A bulge is seated ON the body, in the direction of the finger, rather
    // than at the finger's own depth. Seating it at the finger would have put
    // it inside the body for a touch near the centre (invisible) and adrift in
    // empty space for a touch near the edge (a detached ball); seated on the
    // surface it always fuses, and the direction is what carries the gesture.
    gBulgeN = 0;
    for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
        if (i >= uTouchCount) break;
        vec4 tp = uTouchPoints[i];
        if (tp.z <= 0.0) continue;
        vec2 fw = vec2(tp.x * gAspect, tp.y) * gViewScale;
        gBulgeC[gBulgeN] = normalize(vec3(fw, -BULGE_DEPTH)) * BULGE_SEAT;
        gBulgeR[gBulgeN] = BULGE_R * tp.z;
        gBulgeN++;
    }
    // The cull sphere only grows once there is something outside the body to
    // contain, so an untouched frame culls against the body's own radius and
    // marches exactly the volume it did before touch existed.
    gSceneBound = gBulgeN > 0 ? TOUCH_BOUND : BODY_BOUND;

    // ---- camera ------------------------------------------------------------
    vec3 ro = vec3(0.0, 0.0, -CAM_DIST);
    vec3 rd = normalize(vec3(uv * gViewScale, CAM_DIST));

    // Analytic entry into the bounding sphere. Everything this style draws is
    // inside it, so a ray that misses it can skip the march entirely - which
    // is most of the screen, and the difference between this style running on
    // a phone and not.
    float b = dot(ro, rd);
    float cc = dot(ro, ro) - gSceneBound * gSceneBound;
    float h = b * b - cc;

    // The palette coordinate walks with the morph clock rather than with the
    // segment index: 0.22 of the ramp per segment gives each skeleton its own
    // colour identity, and because the clock is continuous the identity slides
    // into the next one instead of cutting at the segment wrap (an index-based
    // hue jumped a quarter of the ramp on every change).
    float baseHue = 0.08 + clock * 0.22 + 0.10 * mid;
    // Once, not twice: this is two octaves of 3D value noise, and the fog mix
    // below wants the same room the miss branch draws.
    vec3 roomCol = room(rd, uv, baseHue);
    vec3 col = roomCol;
    float glow = 0.0;

    if (h > 0.0) {
        float sh = sqrt(h);
        float tExit = -b + sh;
        // Per-pixel start offset, taken out at the bounding sphere where
        // there is never any geometry. Every ray otherwise samples the
        // proximity glow at the same depths and the integral quantizes into
        // concentric shells around the body - contour lines drawn on the haze
        // rather than haze. It moves where a ray SAMPLES, not where it is
        // allowed to stop: the first step out of here is still bounded by the
        // distance estimate like every other.
        float jitter = hash11(dot(gl_FragCoord.xy, vec2(0.7548776, 0.5698402)));
        float t = max(-b - sh, 0.0) + jitter * 0.01;

        float hitT = -1.0;
        float travelled = 0.0;
        float hitFuse = 0.0;
        float hitFront = 0.0;

        for (int i = 0; i < MARCH_MAX; i++) {
            if (float(i) >= uSteps) break;
            vec3 p = ro + rd * t;
            float d = map(p);
            float eps = max(HIT_EPS * t, 6e-4);
            if (d < eps) {
                hitT = t;
                travelled = float(i) / max(uSteps, 1.0);
                hitFuse = gFuse;
                hitFront = gFront;
                break;
            }
            // Floored at eps because a step finer than the surface threshold
            // cannot resolve anything and a zero step spends the rest of the
            // budget standing still.
            float adv = max(d * STEP_SCALE, eps);
            // Proximity glow: the body leaks light into the room around it, so
            // it is seated in the scene instead of pasted on top of it. The
            // segment length is capped inside the integral only - capping the
            // step itself would waste budget crossing the empty part of the
            // bounding sphere.
            glow += exp(-d * 7.0) * min(adv, 0.12);
            t += adv;
            if (t > tExit) break;
        }

        if (hitT > 0.0) {
            vec3 p = ro + rd * hitT;
            vec3 n = normalAt(p, max(HIT_EPS * hitT, 8e-4));
            float ao = occlusion(p, n, max(hitT * 0.02, 0.02));
            // A ray that needed most of its budget was crawling through folded
            // geometry, and that is exactly where the creases are.
            ao *= 1.0 - 0.25 * travelled;

            vec3 key = normalize(vec3(0.45, 0.72, -0.52));
            vec3 fill = normalize(vec3(-0.65, -0.20, 0.55));
            float dif = clamp(dot(n, key), 0.0, 1.0);
            float bnc = clamp(dot(n, fill), 0.0, 1.0);
            float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);

            // THE SUBJECT. Fusion seams and the metamorphic frontier are one
            // quantity as far as the eye is concerned - both are places where
            // the surface is being negotiated between two descriptions of it -
            // so they are summed and then read as one heat.
            float heat = clamp(hitFront + 0.85 * hitFuse, 0.0, 1.5);

            float hue = baseHue + 0.07 * n.y + 0.06 * heat;
            vec3 body = pal(hue);
            vec3 rim = pal(hue + 0.30);
            vec3 hot = pal(hue + 0.55);

            // Little ambient on purpose. A fill floor high enough to see into
            // the shadow side is what made the first pass read as a pastel
            // balloon: with four skeletons that differ mostly in the SHAPE of
            // their shadows, the shadows have to be dark enough to have shape.
            col = body * (0.09 + 0.88 * dif + 0.20 * bnc) * ao;

            // Treble sharpens the silhouette, and the band under the surface's
            // own height sharpens it further, so the outline sings the
            // spectrum from the bottom of the body to the top.
            float band = aband(clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
            col += rim * fres * (0.26 + 0.42 * treb + 0.30 * band);
            col += vec3(1.0) * pow(clamp(dot(n, normalize(key - rd)), 0.0, 1.0), 40.0)
                * (0.22 + 0.30 * treb) * ao;

            // Squared, so the heat is a thin bright line on the seam rather
            // than a wash over the whole body: a narrow response is what makes
            // it read as an edge, and it is also what keeps a full-body morph
            // from becoming a large-area luminance change.
            col += hot * heat * heat * (0.72 + 0.55 * energy);
            // A white core down the middle of that line. The fifth power is
            // what keeps it a core: at half the heat it is already a
            // thirty-second of its peak while the squared seam term above is
            // still at a quarter of its own, so the white sits inside the
            // colour rather than spreading out to the same width as it.
            col += vec3(1.0) * pow(clamp(heat, 0.0, 1.0), 5.0) * 0.38;

            // The shockwave, as light. The 0.03-unit geometric ridge above is
            // most of a pixel at the silhouette and nothing at all face-on;
            // the same profile added to the heat is what actually makes a beat
            // visible as a ring crossing the skin, and it costs one exp.
            if (gShellA > 0.0) {
                float u = (length(p) - gShellR) / SHELL_W;
                col += hot * exp(-u * u) * beatEnv * beatEnv * 0.35;
            }

            // The finger's own highlight, in the folded frame: touchAnchor()
            // is not pushed through view()'s kaleidoscope or tiling, and using
            // the folded uv here is deliberate - the same mirrors that repeat
            // the body repeat the mark the finger leaves on it.
            col += hot * touchFalloff(uv, 0.55) * 0.30;

            // Into the room with distance, so the far side of the body sits
            // behind the near side instead of beside it.
            float fog = smoothstep(CAM_DIST - BODY_BOUND, CAM_DIST + BODY_BOUND, hitT) * 0.45;
            col = mix(col, roomCol + vec3(0.006, 0.005, 0.012), fog);
        }
    }

    col += pal(baseHue + 0.42) * glow * 0.45 * (0.45 + 0.55 * energy);

    // Body, rim, specular, seam, core, shockwave, touch and haze all added
    // together: HDR by construction. Clipping would flatten every rim and every
    // seam into the same white; the exponential rolls them off instead and,
    // being bounded by 1, is also what guarantees that no combination of audio
    // can produce a full-screen white frame. Energy leans on the exposure by a
    // tenth and no more, for the same reason.
    float exposure = 0.98 * (0.92 + 0.16 * energy);
    col = vec3(1.0) - exp(-max(col, vec3(0.0)) * exposure);

    // A soft vignette. Gentle rather than kaleido's hard fade to black: this
    // composition has a lit room in it that should not go dark at the corners.
    col *= 1.0 - 0.22 * smoothstep(0.55, 2.0, length(uv));

    fragColor = vec4(grade(col), 1.0);
}
