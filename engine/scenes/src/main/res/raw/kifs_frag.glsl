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
//  KIFS - a kaleidoscopic crystal cathedral, breathing.
//
//  A Kaleidoscopic Iterated Function System: fold, rotate, scale, repeat.
//  Each round takes abs() of the point (three mirror planes at once), sorts
//  its components (three more mirrors - in x=y, x=z and y=z), turns the
//  result, magnifies it about a centre and reflects z in a plane. Six or seven
//  rounds of that on one box build a mirrored architecture with real depth.
//
//  THE CORE IS THE ONE IN THIS REPO ALREADY. hyperspace_frag's deTemple - the
//  sorted-fold Menger with the z REFLECTION rather than the conditional
//  translation that made it render as dust - is the fold set here, at its own
//  numbers (magnify by 3 about (1,1,0), mirror z at 1, seed box of half-extent
//  1). Everything this style adds is a continuous deformation of those:
//  the magnification, the centre, the mirror plane and two rotations all move,
//  and the seed box crossfades to an octahedron. Starting from a fold set that
//  is known to render as a surface, rather than from a plausible one, is the
//  difference between architecture and static - and the fold set is exactly
//  where that goes wrong invisibly.
//
//  WHAT MAKES IT ALIVE rather than a poster of a fractal is the ROTATION
//  BETWEEN THE FOLDS, together with the drift of the magnification centre and
//  of the mirror plane. All of them are continuous functions of time, and
//  because every step of the loop is an isometry or a uniform scale, moving
//  them reorganizes the whole cathedral without ever cutting: piers grow into
//  arches, arches close into spires, spires open again. That is the primary
//  motion of this style. The audio modulates it; it does not create it, which
//  is why silence still shows a cathedral rebuilding itself rather than a
//  frozen fractal or a black screen.
//
//  WHY THE ESTIMATE IS VALID, and why the fold set may be animated freely.
//  abs(), the three sorting swaps, the z reflection and the optional angular
//  pre-fold are all reflections; the two rotations are rotations. Every one of
//  them is 1-Lipschitz, so n rounds compose to an S^n-Lipschitz map. The seed
//  is 1-Lipschitz too (see kifsSeed), so seed(F^n(p)) is S^n-Lipschitz, and
//  dividing it by S^n gives a 1-Lipschitz function vanishing exactly on the
//  surface - which is the definition of an estimate that can never
//  OVERESTIMATE. Two consequences worth stating: an animated isometry is still
//  an isometry, so the folds may move as fast as taste allows without the
//  march walking through anything, and the ray may take the FULL estimate as
//  its step rather than the usual 0.8 fudge.
//
//  DELIBERATELY NOT HERE: a normal-direction ambient-occlusion pass. The
//  occlusion is read off the march step count instead - a ray that needed most
//  of its budget was crawling down a fold, which is exactly where the creases
//  are - and that costs nothing at all, where the usual two-tap AO would be
//  two more evaluations of the most expensive loop on the frame. It is also
//  the term that makes the interior read as architecture instead of as a flat
//  mirrored pattern, so it is not an optimisation with a cost, it is the cheap
//  thing that happens to be the right thing.
// ===========================================================================

#define KIFS_TAU 6.2831853

/**
 * Compile-time ceiling on the fold loop. See lib_sdf3's RAYMARCH note.
 *
 * Nine, not the twenty a still image would use, because past a certain depth
 * the loop draws detail finer than a pixel, and sub-pixel fold detail does not
 * read as detail - it reads as the static this style renders as when it is
 * wrong. Each round divides the cell by the magnification (about 2.9): at six
 * rounds the smallest cell is about 0.0037 world units against a pixel
 * footprint of about 0.0029 at this framing on a 1080-tall surface, which is
 * the crossing point, and the seventh round is already half a pixel and is
 * carried by the normal epsilon smoothing it rather than by resolving it. The
 * runtime budget therefore tops out at 7, and this cap leaves the beat step
 * room above it.
 */
#define KIFS_MAX_ITERS 9

/** Compile-time ceiling on the march: MarchBudget.MAX_STEPS. uSteps is the runtime budget. */
#define KIFS_MAX_STEPS 128

// ---- the cathedral's resting shape ----------------------------------------

/**
 * The magnification per round.
 *
 * deTemple's Menger value is 3, where each round removes exactly the middle
 * third and the result is a lattice of thin bars. A little under it removes
 * less per round, so the piers keep their mass and the model reads as masonry
 * rather than as wire - which is what a cathedral is made of. The base sits
 * low in the range that renders as a surface (about 2.5 to 3.3 here) so that
 * the bass swing below has somewhere to go without leaving it.
 */
const float KIFS_SCALE_BASE = 2.86;

/**
 * How far bass moves the magnification: the breath.
 *
 * The invariant box is set by the CENTRE, not by the magnification (see
 * gSeedB), so moving this subdivides the interior more or less finely while
 * the silhouette holds still - the cathedral breathes without changing size,
 * which is what stops the breath from reading as a zoom. Small on purpose:
 * uBass is a per-frame band envelope and the magnification is compounded seven
 * times, so 0.30 here is already a factor of two in the depth of the
 * structure. Any more and a jumpy envelope reads as flicker rather than
 * breathing, which is the one thing the audio rules forbid.
 */
const float KIFS_SCALE_BASS = 0.30;

/** The slow ambient half of the breath, so it breathes in silence too. */
const float KIFS_SCALE_DRIFT = 0.10;

/**
 * The resting magnification centre - the point the folds are taken about, and
 * (see gSeedB) the half-extent of the cell as well.
 *
 * deTemple's (1, 1, 0): the third component is zero because z is the axis the
 * mirror plane handles, and giving it an offset as well double-counts. Letting
 * it drift a little either side of zero is what tilts the galleries.
 */
const vec3 KIFS_C_BASE = vec3(1.0, 1.0, 0.0);

/**
 * How far the centre wanders, and the range it is held inside.
 *
 * Every 0.01 of it moves EVERY level of the structure, so this is the loudest
 * of the ambient motions and the reason the idle frame never repeats. The
 * clamp is what keeps the model inside the parameters it is known to render
 * in: the sorted folds only clear the offset over a band around 1, and outside
 * it the sponge either fills in solid or shatters. Touch pushes against this
 * clamp rather than past it - a gesture may reorganize the cathedral, it may
 * not break it.
 */
const float KIFS_C_SWING = 0.14;
const vec3 KIFS_C_MIN = vec3(0.66, 0.66, -0.26);
const vec3 KIFS_C_MAX = vec3(1.38, 1.38, 0.26);

/**
 * How far a finger may drag the magnification centre, before the clamp above.
 *
 * Larger than the ambient swing, so a touch always dominates what the drift is
 * doing - a gesture that reads as "the cathedral happened to move" is not a
 * gesture.
 */
const float KIFS_TOUCH_PUSH = 0.42;

/**
 * Escape radius as a multiple of the cell's corner distance, and the slack on
 * it.
 *
 * Outside a ball this size nothing can be hit, so kifsMap returns the distance
 * to the ball without running the loop, and a ray that never enters it costs
 * one length() per step. The structure lives inside the cell box, whose corner
 * is length(gSeedB) away, so 1.0 would already bound it; 1.25 is the margin
 * for the two rotations, which are not part of that containment argument and
 * do carry geometry a little outside the box. Too small clips the outer spires
 * against an invisible sphere, too large costs a handful of march steps - so
 * it is deliberately biased large.
 */
const float KIFS_BOUND_K = 1.25;
const float KIFS_BOUND_SLACK = 0.25;

// ---- the folds ------------------------------------------------------------

/** Resting angle of the rotation applied between the mirror folds and the magnification. */
const float KIFS_FOLD_BASE = 0.21;

/**
 * How far that angle swings, and how fast.
 *
 * Held under about half a radian in total, touch aside. A KIFS tolerates a rotation between its
 * folds up to roughly that; past it successive rounds stop landing on each
 * other, the surfaces break into disconnected fragments, and the result reads
 * as noise while costing a full march budget per pixel to draw. The rate is
 * slow enough that the reorganization is something you notice having happened
 * rather than something that flickers.
 */
const float KIFS_FOLD_SWING = 0.17;
const float KIFS_FOLD_RATE = 0.061;

/** Mids steer the fold planes. Small for the same reason KIFS_SCALE_BASS is. */
const float KIFS_FOLD_MID = 0.05;

/** The beat-locked lurch of the fold angle: the cathedral flinches on the hit and settles. */
const float KIFS_FOLD_BEAT = 0.045;

/**
 * Three-plus fingers twist the folds directly.
 *
 * uTouchSpin is a signed angular rate that TouchField clamps to +-8, and a
 * flick reaches a couple of that; 0.055 puts a hard swirl at about a fifth of
 * a radian, which is most of the fold angle's whole range. That is the point -
 * the angle between the mirror planes is what the cathedral IS, so having it
 * under a hand should feel like twisting the building.
 */
const float KIFS_TOUCH_SPIN = 0.055;

/** The second, cheaper rotation - a mat2 on xz rather than a full mat3, the way deTemple spells it. */
const float KIFS_POST_BASE = -0.14;
const float KIFS_POST_SWING = 0.16;
const float KIFS_POST_RATE = 0.0431;

/**
 * Where the z mirror plane sits, and how far it travels.
 *
 * This is the carve, and it is also the cell's z half-extent (gSeedB.z). High
 * and the plane sits above the cell so almost nothing is removed - a solid
 * crystal; low and it cuts deep - an open lattice with windows all the way
 * through. Travelling between the two is the single most cathedral-like thing
 * this style does, and it is why the plane drifts rather than sitting at
 * deTemple's fixed 1.0. Floored well above zero: the reflection is only
 * norm-non-increasing for a plane on the positive side of the origin, and a
 * zero-thickness cell is a zero-volume model.
 */
const float KIFS_ZM_BASE = 0.86;
const float KIFS_ZM_SWING = 0.26;
const float KIFS_ZM_RATE = 0.047;
const float KIFS_ZM_FLOOR = 0.45;

// ---- the seed -------------------------------------------------------------

/**
 * The octahedron's vertex radius as a multiple of the cell's mean half-extent.
 *
 * An octahedron of radius r has its faces at r/sqrt(3), so 1.73 is the one
 * that just touches the cell's faces and reads as the same size as the box.
 * 1.60 is a little tighter, which is what makes the crossfade below read as
 * the cell being carved back to its diagonals rather than as it changing
 * shape at constant volume.
 */
const float KIFS_SEED_OCT = 1.60;

/**
 * Edge rounding, in the FOLDED frame - so it is 3% of a cell at EVERY level
 * rather than 3% of the whole model at the coarsest one. A constant in world
 * units would round the outer piers and leave the inner ones knife-edged.
 */
const float KIFS_SEED_ROUND = 0.03;

/**
 * Ceiling on the box-to-octahedron crossfade.
 *
 * Not 1. The box is the cell the fold maps back into itself, and the further
 * the seed gets from it the less of that containment survives; at a full
 * crossfade the corners round off at every level at once and the cathedral
 * turns into coral. Half way is where the vaults appear and the piers are
 * still piers.
 */
const float KIFS_SEED_MIX_MAX = 0.5;

// ---- camera, march and light ----------------------------------------------

/** Far enough out that the camera stays outside the escape ball at every centre this style can reach. */
const float KIFS_CAM_R = 4.15;

/** Bass pushes the camera in as well as subdividing the structure - the two together read as one breath. */
const float KIFS_CAM_PUSH = 0.20;

/**
 * Chosen with KIFS_CAM_R so the cell box very nearly fills the frame height.
 *
 * Long rather than wide: a wide lens on a model this deep puts most of the
 * frame outside the escape ball, which costs nothing to draw and shows
 * nothing. It also shrinks the pixel footprint, which is what pays for the
 * seventh fold round above.
 */
const float KIFS_FOCAL = 2.25;

/**
 * Hit threshold: a slope plus a floor.
 *
 * The slope is a little under the pixel footprint (about 0.00082*t on a
 * 1080-tall surface at this focal length), so a surface is found just inside
 * the pixel it belongs to rather than a pixel early. A constant epsilon
 * shimmers in the distance and wastes steps up close; the floor only matters
 * for the near piers.
 */
const float KIFS_EPS_SLOPE = 0.0006;
const float KIFS_EPS_FLOOR = 4e-5;

/**
 * Ambient occlusion off the march step count: the allowance, the slope and the
 * floor.
 *
 * A ray that spent most of its budget was crawling down a fold, and that is
 * where the creases are. But a distance-estimated fractal converges
 * GEOMETRICALLY on every hit, so even a flat outer face costs about a third of
 * the budget - and darkening from step zero crushed the entire model to the
 * floor and lost the shape it was supposed to reveal. The allowance is that
 * baseline: occlusion starts only past it, so a clean face is unshaded and
 * only the extra steps count. The floor exists because a fully black crease
 * loses the colour that says which shell it belongs to.
 */
const float KIFS_AO_FREE = 0.32;
const float KIFS_AO_DEPTH = 1.15;
const float KIFS_AO_FLOOR = 0.13;

/**
 * Trap value kifsMap reports outside the escape ball: a defined "far", not a
 * huge number, which would map to a wild hue on the one pixel that read it.
 */
const float KIFS_TRAP_FAR = 4.0;

// ---- state shared with kifsMap --------------------------------------------
//
// The fold set is the same for every march step of a fragment, and it costs
// half a dozen sin/cos and two matrix builds. These globals are how it gets
// built ONCE per fragment instead of once per step: the same
// preallocated-mutable-state exception CLAUDE.md carves out for the render hot
// path, in the form GLSL has. gTrapR/gTrapZ run the other way - they are how
// the estimate reports its orbit trap back, since GLSL ES has no out
// parameters on a function called four times from a normal. They are
// overwritten by every kifsMap call, so a hit's traps must be captured BEFORE
// the normal is taken.

mat3 gFoldRot;
mat2 gPostRot;
vec3 gFoldC;
float gScale;
float gIters;
float gZMirror;
/** Half-extents of the cell the whole model is built out of. */
vec3 gSeedB;
float gSeedO;
float gSeedMix;
float gBound;
/** uSymmetry when the user's kaleidoscope is on, 0 when it is off. */
float gSym;
float gTrapR;
float gTrapZ;

/**
 * The seed solid every cell of the cathedral is a copy of.
 *
 * A box and an octahedron crossfaded rather than one or the other: the box's
 * cells meet at faces and build piers and lintels, the octahedron's meet at
 * points and build spires and vaults, and the crossfade walks the whole
 * cathedral between the two without a cut. Both primitives are exact, and a
 * convex combination of two 1-Lipschitz functions is 1-Lipschitz, so the mix
 * is still a lower bound on the distance to its OWN zero set - it is not the
 * distance to some blend of the two solids, and it does not need to be.
 * opRound is a constant offset of the field, so it costs the march nothing.
 */
float kifsSeed(vec3 p) {
    float b = sdBox(p, gSeedB);
    float o = sdOctahedron(p, gSeedO);
    return opRound(mix(b, o, gSeedMix), KIFS_SEED_ROUND);
}

/** The distance estimate, and the orbit trap it leaves in gTrapR/gTrapZ. */
float kifsMap(vec3 p) {
    gTrapR = KIFS_TRAP_FAR;
    gTrapZ = 1.0;

    // Outside the escape ball nothing can be hit, and the distance to the ball
    // is a valid lower bound on the distance to anything inside it. This is
    // what makes the background nearly free: a ray that never enters costs one
    // length() per step instead of seven rounds of folding.
    float outside = length(p) - gBound;
    if (outside > 0.0) return outside;

    // The user's Symmetry/Kaleidoscope controls, in 3D. view() has already
    // folded the SCREEN into uSymmetry wedges; this folds the WORLD into the
    // same number about the vertical, so the cathedral itself gains the
    // rotational symmetry rather than a mirrored photograph of it gaining it -
    // which on the one style named after the kaleidoscope's own operation is
    // the difference between the control meaning something and decorating
    // something. A piecewise isometry (rotate into the wedge, reflect in its
    // bisector), so it does not touch the argument above. Guarded on uniforms,
    // so the whole quad agrees and it costs nothing when the control is off.
    if (gSym >= 2.0) {
        float rr = length(p.xz);
        // atan(0,0) is undefined and one NaN here poisons every step behind
        // it; below this radius the fold is the identity anyway.
        if (rr > 1e-9) {
            float seg = KIFS_TAU / gSym;
            float a = atan(p.z, p.x);
            a = abs(mod(a, seg) - seg * 0.5);
            p.xz = vec2(cos(a), sin(a)) * rr;
        }
    }

    float s = 1.0;
    for (int i = 0; i < KIFS_MAX_ITERS; i++) {
        if (float(i) >= gIters) break;
        // Six mirror planes at once: abs() gives the three coordinate planes,
        // the sorting swaps give x=y, x=z and y=z. Sorting rather than
        // translating is what makes this construction marchable at all - a
        // conditional TRANSLATION is discontinuous, and a discontinuous fold
        // makes the estimate an overestimate by an unbounded factor at the
        // seam. That is the bug deTemple was written to fix, and this shape
        // inherits the fix rather than rediscovering it.
        p = abs(p);
        if (p.x < p.y) p.xy = p.yx;
        if (p.x < p.z) p.xz = p.zx;
        if (p.y < p.z) p.yz = p.zy;
        // The traps are sampled HERE, between the folds and the magnification,
        // so every round measures in the same frame and the bands they colour
        // are comparable across depths. After the sort p.z is the smallest
        // component, so it is the distance to the nearest coordinate plane -
        // the edge trap - while dot(p,p) is the closest approach to the
        // origin, which is what bands the nested shells.
        gTrapR = min(gTrapR, dot(p, p));
        gTrapZ = min(gTrapZ, p.z);
        p = p * gScale - gFoldC * (gScale - 1.0);
        // The carve: a reflection in the plane z = gZMirror, applied only
        // above it. An isometry, and norm-non-increasing for gZMirror > 0.
        p.z = gZMirror - abs(p.z - gZMirror);
        // The rotation between the folds - the whole reason this is a
        // cathedral rather than a Menger cube, and the whole reason it moves.
        //
        // AFTER the magnification, where deTemple puts its own. That position
        // is not a detail: rotating BEFORE it turns the point while the fold
        // planes stay put, so each round enters the next one already off its
        // own lattice, and by the sixth the surfaces have eroded into a crust
        // with no flat faces left - it renders, it is even Lipschitz-correct,
        // and it looks like coral. Rotating after it turns the whole
        // magnified CELL, so the levels stay square to each other and what
        // moves is the way they stack: piers lean into buttresses, floors
        // become vaults. Same two matrices, same cost, completely different
        // building.
        p = gFoldRot * p;
        p.xz = gPostRot * p.xz;
        s *= gScale;
    }
    // Pull the seed distance back into world units through the accumulated
    // magnification. Without this division the estimate is S^n times too large
    // and the ray walks straight through the model.
    return kifsSeed(p) / s;
}

/** Tetrahedral normal: four kifsMap evaluations rather than the six a central difference needs. */
vec3 kifsNormal(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(
        k.xyy * kifsMap(p + k.xyy * e) +
            k.yyx * kifsMap(p + k.yyx * e) +
            k.yxy * kifsMap(p + k.yxy * e) +
            k.xxx * kifsMap(p + k.xxx * e)
    );
}

/**
 * The vault behind the cathedral. Dim, but never black.
 *
 * A dead-black miss reads as a broken renderer, and it also removes the only
 * cue for how the structure is oriented when it happens to be edge-on. A slow
 * vertical ramp through the user's own palette with a drift on it, so the
 * empty frame is still moving in silence. The levels are low enough that it
 * never competes with the geometry and never contributes a large-area
 * luminance change - the flash budget is a whole-frame quantity, and the sky
 * is the one term here that covers the whole frame.
 */
vec3 kifsSky(vec3 rd, float energy) {
    float band = 0.5 + 0.5 * rd.y;
    float t = 0.55 + 0.34 * band + 0.09 * sin(uTime * 0.019);
    return pal(t) * (0.05 + 0.06 * energy) * (0.55 + 0.45 * band);
}

void main() {
    // view() first: zoom, rotation, drift, kaleidoscope, tiling, pixelate,
    // shake, twist, warp, ripple and the beat pulse all live in there, and a
    // style that builds its own screen coordinates silently drops fifteen of
    // the user's controls.
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
    float beatBump = pow(0.5 + 0.5 * cos(KIFS_TAU * uBeatPhase), 2.0);
    float hit = beatEnv * beatEnv * beatBump;

    // ---- camera -----------------------------------------------------------
    //
    // It orbits forever on two unrelated slow rates, so the cathedral is seen
    // from a new angle every second even in silence, and the yaw never repeats
    // against the pitch inside a listening session. The pitch stays under 0.35
    // rad, which is also what keeps the world up vector well away from the
    // view direction so the basis below can never degenerate.
    float yaw = uTime * 0.037;
    float pitch = 0.34 * sin(uTime * 0.0231);
    float camR = KIFS_CAM_R - KIFS_CAM_PUSH * bassA;
    vec3 ro = vec3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw)) * camR;
    vec3 fwd = normalize(-ro);
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), fwd));
    vec3 up = cross(fwd, right);
    vec3 rd = normalize(right * uv.x + up * uv.y + fwd * KIFS_FOCAL);

    // ---- the fold set, built once per fragment ----------------------------

    // TOUCH, first gesture: the anchor moves the point the folds are taken
    // ABOUT, which is the one parameter every level of the structure is built
    // from - so a finger does not push the picture around, it re-centres the
    // crystal and the whole architecture reassembles around where it was
    // touched. Mapped through the camera's own right/up so the centre moves
    // toward the point on screen the finger is at. touchAnchor() and
    // touchStrength() are both exactly zero with nothing touched, so this term
    // vanishes bit for bit on an untouched frame.
    vec2 anchor = touchAnchor();
    vec3 foldPush = (right * anchor.x + up * anchor.y) * (KIFS_TOUCH_PUSH * touchStrength());

    gFoldC = clamp(
        KIFS_C_BASE + foldPush + KIFS_C_SWING * vec3(
            sin(uTime * 0.0213),
            sin(uTime * 0.0171 + 1.7),
            sin(uTime * 0.0131 + 3.1)
        ),
        KIFS_C_MIN,
        KIFS_C_MAX
    );

    // Bass subdivides the interior more finely; the silhouette does not move,
    // because the cell below is set by the centre and not by this.
    gScale = KIFS_SCALE_BASE + KIFS_SCALE_BASS * bassA + KIFS_SCALE_DRIFT * sin(uTime * 0.0273);

    gZMirror = max(KIFS_ZM_BASE + KIFS_ZM_SWING * sin(uTime * KIFS_ZM_RATE) - 0.16 * bassA, KIFS_ZM_FLOOR);

    // The cell. x and y are the magnification centre and z is the mirror
    // plane, because those are exactly the surfaces the fold maps back into
    // itself: get this wrong in either direction and the model stops being a
    // sponge - too large and the cell swallows the whole bounded orbit so
    // every point inside the escape ball reports a hit (a screen of static),
    // too small and the levels stop touching and it shatters. Deriving it from
    // the fold rather than hardcoding 1 is what lets the centre and the mirror
    // move at all.
    gSeedB = vec3(gFoldC.xy, gZMirror);
    gSeedO = KIFS_SEED_OCT * (gSeedB.x + gSeedB.y + gSeedB.z) / 3.0;
    // Piers to spires and back, on its own slow clock. Squared-cosine rather
    // than a raw sine so it lingers at each end - the two shapes are the
    // interesting states and the crossfade is the transition between them.
    gSeedMix = KIFS_SEED_MIX_MAX * pow(0.5 + 0.5 * sin(uTime * 0.0157), 2.0);
    gBound = KIFS_BOUND_K * length(gSeedB) + KIFS_BOUND_SLACK;

    // TOUCH, second gesture: two fingers set the axis the folds turn about.
    // uTouchAxis is exactly vec2(0) with fewer than two fingers down, so the
    // weight is exactly 0 and the mix returns the resting axis unchanged.
    // Aspect-corrected the way touchAnchor() is, so the axis on screen is the
    // axis the folds take.
    float aspect = uResolution.x / max(uResolution.y, 1.0);
    vec2 axis2 = vec2(uTouchAxis.x * aspect, uTouchAxis.y);
    // Saturates at about 0.6 NDC of finger separation - a comfortable pinch -
    // so the axis is fully the user's before their hand is at full spread.
    float axisW = clamp(length(axis2) * 1.6, 0.0, 1.0);
    // The z component keeps the axis out of the screen plane at full weight; a
    // fold axis lying exactly in the plane makes the two rotations degenerate
    // and the structure flattens.
    vec3 foldAxis = mix(vec3(0.36, 0.90, 0.24), vec3(axis2, 0.55), axisW);

    // TOUCH, third gesture: three or more fingers drive the fold angle
    // directly. Clamped because TouchField's spin is a rate and a flick can
    // reach its own +-8 limit.
    float spin = clamp(uTouchSpin, -6.0, 6.0);
    float foldAngle = KIFS_FOLD_BASE
        + KIFS_FOLD_SWING * sin(uTime * KIFS_FOLD_RATE)
        + KIFS_FOLD_MID * midA
        + KIFS_FOLD_BEAT * hit
        + KIFS_TOUCH_SPIN * spin;
    gFoldRot = rotAxis(foldAxis, foldAngle);

    float postAngle = KIFS_POST_BASE
        + KIFS_POST_SWING * sin(uTime * KIFS_POST_RATE + 1.9)
        + 0.04 * midA;
    gPostRot = rot2(postAngle);

    gSym = (uKaleido > 0.5 && uSymmetry >= 2.0) ? uSymmetry : 0.0;

    // Detail buys fold depth as well as march steps: uSteps runs 64..128, and
    // the extra rounds are what the extra steps are for. The BEAT steps it by
    // one more - the only discrete thing on the frame, per the audio rules.
    // One extra round adds a level of structure finer than everything already
    // on screen, so the silhouette does not move and the surfaces gain texture
    // instead: a crystallization on the hit rather than a pop. Scaled by the
    // user's Beat response, so setting that to zero really does stop it.
    //
    // A held snap - a fold angle jumping to a NEW value on each beat and
    // staying there until the next one - is what this wanted to be, and it is
    // not available: a fragment shader has no state between frames, and there
    // is no beat INDEX in the uniform contract to hash. uBeatPhase says how
    // long ago the last transient was, not which transient it was, and
    // recovering an index from it needs the tempo, which is not uploaded. The
    // lurch in foldAngle above is the continuous form of the same gesture.
    float iterBeat = step(0.30, hit * clamp(uBeatResponse, 0.0, 2.0));
    gIters = mix(5.0, 7.0, clamp((uSteps - 64.0) / 64.0, 0.0, 1.0)) + iterBeat;

    // ---- march ------------------------------------------------------------
    //
    // Entered analytically at the escape ball rather than from the camera. A
    // ray that misses the ball is a background pixel and skips the loop
    // entirely, which on a typical frame is most of them.
    float bq = dot(ro, rd);
    float cq = dot(ro, ro) - gBound * gBound;
    float hq = bq * bq - cq;
    float tEnter = 0.0;
    float tExit = -1.0;
    if (hq > 0.0) {
        float rootq = sqrt(hq);
        // max(0) because the camera can sit inside the ball at the top of the
        // centre's range, and a negative start would march backwards.
        tEnter = max(-bq - rootq, 0.0);
        tExit = -bq + rootq;
    }

    // Started one epsilon INSIDE the ball rather than on it. kifsMap returns
    // exactly 0 on the ball's surface - that IS the early-out - and 0 passes
    // the hit test below, so entering on the boundary made every ray report a
    // hit on an invisible sphere and the whole frame came out as static. One
    // epsilon in, the first sample is already past the early-out and reads
    // real geometry.
    float t = tEnter + KIFS_EPS_SLOPE * tEnter + KIFS_EPS_FLOOR;
    float hitT = -1.0;
    float used = 0.0;
    float trapR = KIFS_TRAP_FAR;
    float trapZ = 1.0;
    // Only ever read to pick a hue for the halo below - the closest the ray got
    // says which shell it was threading past, and that is the colour the light
    // coming through should be.
    float glowTrap = KIFS_TRAP_FAR;
    float nearest = 1e9;

    for (int i = 0; i < KIFS_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > tExit) break;
        // Steps CONSUMED, for the occlusion and the halo. Counted here so a
        // ray that runs out of budget or leaves the ball reports what it
        // spent; the hit branch overwrites it with the steps taken BEFORE
        // landing, since the step that lands is the one that found the
        // surface rather than one spent looking for it.
        used = float(i) + 1.0;
        vec3 p = ro + rd * t;
        float d = kifsMap(p);
        float eps = KIFS_EPS_SLOPE * t + KIFS_EPS_FLOOR;
        if (d < eps) {
            hitT = t;
            used = float(i);
            trapR = gTrapR;
            trapZ = gTrapZ;
            break;
        }
        if (d < nearest) {
            nearest = d;
            glowTrap = gTrapR;
        }
        // The FULL estimate, not the usual fraction of it. The Lipschitz
        // argument at the top of this file is what earns that: every fold is
        // an isometry and the magnification is divided back out, so the
        // estimate can never overestimate and a shortened step would only buy
        // slower convergence for the same picture.
        t += max(d, eps);
    }

    float budget = max(uSteps, 1.0);
    vec3 sky = kifsSky(rd, enA);
    vec3 col;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        // The normal epsilon tracks the pixel footprint rather than being a
        // constant: too small and the normal is quantization noise at grazing
        // angles, too large and every edge rounds off. Slightly wider than the
        // hit epsilon so the finest fold level is smoothed rather than
        // sampled, which is what stops the surface from sparkling as the
        // camera moves.
        float e = max(KIFS_EPS_SLOPE * 1.6 * hitT, 6e-5);
        vec3 n = kifsNormal(p, e);

        // Ambient occlusion, free: how much of its budget the ray spent.
        float ao = clamp(1.0 - KIFS_AO_DEPTH * max(used / budget - KIFS_AO_FREE, 0.0), KIFS_AO_FLOOR, 1.0);

        // Two fixed WORLD lights. Fixed, not view-locked, so the cathedral
        // turning through them is what reveals its shape - a headlight would
        // flatten every fold into the same grey.
        vec3 key = normalize(vec3(0.42, 0.78, -0.46));
        vec3 fill = normalize(vec3(-0.66, 0.12, 0.74));
        float dif = clamp(dot(n, key), 0.0, 1.0);
        float bnc = clamp(dot(n, fill), 0.0, 1.0);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.5);

        // Treble sharpens the edge: a tighter specular lobe reads as harder,
        // more crystalline material without touching the geometry, which is
        // the only thing a per-frame treble value is safe to drive.
        float spec = pow(clamp(dot(n, normalize(key - rd)), 0.0, 1.0), 22.0 + 90.0 * trebA);

        // The orbit trap bands the surface into nested shells. log() of the
        // SQUARED radial trap, because the shells are geometrically spaced -
        // every round of the loop is a magnification - so only a logarithm
        // spreads them evenly across the palette instead of crushing the deep
        // ones into one colour.
        float band = log(max(trapR, 1e-6)) * 0.11;
        float shell = band + 0.22 * trapZ + 0.07 * sin(uTime * 0.029) + 0.06 * midA;
        vec3 body = pal(shell);
        // A related but distinct hue for the rim: 0.28 of a turn is far enough
        // to read as a different material and close enough to still be the
        // same building.
        vec3 rimCol = pal(shell + 0.28);

        // The shell is lit by its OWN band of the spectrum - the edge trap
        // indexes the analyser - so a bassline lights the deep shells and a
        // hi-hat the near ones, instead of everything brightening at once.
        // 0.55 is the floor, so with silence the rim is still there.
        float lit = aband(clamp(trapZ * 2.2, 0.0, 1.0));

        col = body * (0.18 + 0.80 * dif + 0.32 * bnc) * ao;
        col += rimCol * fres * (0.34 + 0.5 * trebA) * (0.55 + 0.9 * lit) * ao;
        col += mix(vec3(1.0), rimCol, 0.5) * spec * 0.30 * (0.3 + 0.7 * trebA) * ao;

        // Depth haze, measured from the ball entry rather than from the
        // camera, so the near face is unfogged and only the depth INSIDE the
        // structure is greyed. That is what separates the front arcade from
        // the ones behind it.
        float fog = 1.0 - exp(-max(hitT - tEnter, 0.0) * 0.18);
        col = mix(col, sky, fog);
    } else {
        col = sky;
        // The light through the arcades. A ray that missed but spent most of
        // its budget was threading a window - creeping past surface after
        // surface without ever landing on one - so the step count IS the
        // measure of how much structure it passed through, and it is already
        // paid for. Keyed on the step count rather than on an exp() of the
        // estimate per step: a KIFS estimate is a severe UNDERestimate away
        // from the surface (it is divided by S^n), so a per-step exponential
        // of it lights the whole escape ball evenly and the frame comes out as
        // flat fog. Squared so only the rays that really crawled contribute.
        float threaded = used / budget;
        float glowHue = log(max(glowTrap, 1e-6)) * 0.16 + 0.46;
        col += pal(glowHue) * threaded * threaded * (0.55 + 0.9 * enA) * (1.0 + 0.25 * hit);
    }

    // TOUCH, everywhere: the wake. Immediate feedback under the finger, before
    // the geometry has finished reassembling, and it survives the finger
    // lifting because touchWake sums the fading slots too. Fed the FOLDED uv
    // rather than raw screen coordinates on purpose - with the kaleidoscope on
    // the wake is mirrored into every wedge, which is what a kaleidoscope
    // should do to a finger. touchWake is unbounded above (five fingers sum to
    // five), so it is clamped before it can become a flash.
    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }

    // A gentle vignette rather than kaleido's full one: this is a lit 3D
    // scene, and crushing the corners to black would eat the outer arcades
    // that give it its scale.
    col *= 0.55 + 0.45 * smoothstep(2.0, 0.4, length(uv));

    fragColor = vec4(grade(col), 1.0);
}
