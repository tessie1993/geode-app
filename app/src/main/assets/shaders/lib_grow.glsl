// Grow and layers: Spawn, Fractal, Front layer and Back layer (core/viz/ParamTable.inc
// spawn .. backLayer, uploaded by ShaderScene as uSpawn .. uBackLayer), the shape params
// the music drives through the reaction routes, waves, envelopes and macro knobs. One
// set of rules the family libraries share, so "spawn" or "the front layer" means the
// same in every family; each library applies them to its own field, since a library
// cannot hand its style's map to a function here (GLSL has no function values).
//
// THE DEFAULT IS THE OLD PICTURE. Every reader below is behind a test on its uniform
// being off (exactly 0 at the default), and an off helper returns its input, exact
// zero or exact one, so a library at the defaults takes the path it took before these
// existed, bit for bit.
//
// NO COLOUR AND NO TURN. These move geometry only: no palette read, no brightness, no
// contrast (the owner's colour rule). Nothing here rotates (the owner removed spin):
// a spawned copy slides, a layer opens, a child copy grows; none of them turns. No
// clock: the music moves these through their params, so nothing here reads uTime
// (and no clock times a live dial, the shaders' time rule).
//
//   Life         births (below, "Life's births"): up to three more copies of the
//                family's world, each grown from nothing at a spawn point of its own
//                off the centre, drifting off it, then collapsing in on itself
//                (implode, fold back or cave in). core/viz/Births decides when and
//                where on the scene's own Speed-integrated clock and hands them over
//                as uBirths; a family reads the map once for each in further passes
//                of the loop it already runs. Life 0 bears nothing, every birth pass
//                is skipped, and the slack below stays exactly 0.
//
//   Spawn        a second copy of the family's world, unioned with the first. At 0 it
//                lies exactly on the first (the union is the first); as Spawn rises it
//                slides off by growSpawnShift, so the petals, shards, threads or
//                rosettes split in two and the new ones ease away between the old.
//   Fractal      the world again at a third of its size, repeated, kept only within a
//                shell round the first's surface that thickens as Fractal rises:
//                smaller self-similar copies grow on the subject and fractal out.
//   Front layer  how far the foreground opens (up) or draws in (down). The layers
//                are depth: for a style flown through (camFly) the few units round the
//                eye are the front layer and the way ahead the back; for a style built
//                round one subject (camAround) the subject is the front layer and what
//                lies beyond it the back. Opening scales space about the axis the eye
//                rides (or about the subject), so walls stand back or close in and a
//                subject swells or shrinks, never into the eye. A family with a
//                backdrop of its own (a sky, a field, a veil) moves that with the back
//                layer as well (growBackdrop).
//   Back layer   the same for the background.
//
// INCLUDE ORDER: lib_scene_uniforms includes it, after the uniforms it reads.

/**
 * Where the layers are measured from, the depth (world units) where the front layer gives way to the
 * back, and whether the eye turns about a subject at the origin (camAround) rather than riding an
 * axis (camFly). lib_camera sets them; a library that moves the eye into its own frame (a wrap into
 * one period) says so again with growEye.
 */
vec3 gGrowEye = vec3(0.0);
float gGrowNear = 3.0;
bool gGrowAround = false;
/**
 * Whether lib_camera has set the three above this pixel (camFly or camAround). A style that draws from a
 * view of its own never calls them, and a reader that falls back on the rig's frame (lib_shell) must
 * know the defaults above are only placeholders then.
 */
bool gGrowSet = false;
/**
 * The rig's view basis (right, up, forward), which lib_camera sets with the frame: births are placed and
 * fold on the screen's axes, so they are seen where core/viz/Births puts them, whichever way the eye faces.
 */
mat3 gGrowBasis = mat3(1.0);

void growSetup(vec3 eye, float near, bool around) {
    gGrowEye = eye;
    gGrowNear = near;
    gGrowAround = around;
    gGrowSet = true;
}

/** The eye in the frame the library's field is evaluated in. */
void growEye(vec3 eye) {
    gGrowEye = eye;
}

/** Whether all four rest at exactly 0 (their default): a library's field is then its map alone, read once, as before. */
bool growIdle() {
    return uSpawn == 0.0 && uFractal == 0.0 && uFrontLayer == 0.0 && uBackLayer == 0.0;
}

/**
 * The frame for a style that frames its own view (no camFly or camAround this pixel, and no growSetup):
 * taken as built round a subject at the origin, which its own eye orbits. A style that flies an eye of
 * its own says so with growSetup(eye, 3.0, false) instead. Only called while a control is on.
 */
void growFrame() {
    if (!gGrowSet) gGrowAround = true;
}

/** The point a library's world opens about: the subject at the origin, or the axis the eye rides. */
vec3 growCentre() {
    return gGrowAround ? vec3(0.0) : vec3(gGrowEye.xy, 0.0);
}

// ---- Life's births ----------------------------------------------------------------
//
// A birth is the family's world again, scaled (0 while unborn, up to 0.35 grown), in a ball round its spawn
// point and nowhere else: a style built round a subject sees its subject again, small, off to one side; a
// flown world sees a little of the world round a point ahead of the eye, in miniature, hanging before it.
// Spawn points are in birth units, 0.3 of the frame's near depth (of the reach in 2D), on the screen's axes.
// Each dies by its kind: Implode shrinks it into its middle; Fold back reflects what lies past a plane that
// sweeps from its far rim to its near one back over the plane, so it folds onto itself until nothing is left;
// Cave in opens a hollow on one side that grows until it has eaten the whole ball. A slide, a scale and a
// reflection only: nothing turns, and no colour is read or written.
//
// How a family runs them: passes 3, 4 and 5 of the field loop it already runs (one map call a pass, in a
// loop that starts at a uniform), each behind growBorn(pass - 3): the map read at growBirthAt, its distance
// kept through growBirthDist if nearer. A density adds its density times growBirthMask; a 2D pattern is
// read at growBirth2 and laid over by growBirthMask; a volume read once a step reads at growBirthsAt. growBirthIn and growBirth2 leave the read's state in the
// globals below for the other two.

/** A birth's ball: its radius in its own space, birth units. */
#define GROW_BIRTH_BALL 2.0
/** How far ahead of a flown eye a birth hangs, birth units. */
#define GROW_BIRTH_AHEAD 2.5

float gGrowBirthS = 1.0;         // the read's scale: world units per unit of the birth's own space
vec3 gGrowBirthR = vec3(0.0);    // the point in the birth's own space, folded back
float gGrowBirthBall = 1.0;      // its ball's radius in that space
vec3 gGrowBirthSide = vec3(0.0); // the rim point its hollow opens at, in that space
float gGrowBirthCave = 0.0;      // the hollow's radius (0: none)

/** Whether birth b (0..2) is alive this frame: never at Life 0, so its pass is skipped and the field is the old one. */
bool growBorn(int b) {
    return uLife > 0.0 && uBirths[2 * b].w > 0.0;
}

/** Keep a read's state: r (the birth's own space) folded back for a Fold back birth, `side` its unit side. */
void growBirthKeep(vec3 r, vec3 side, vec4 how, float ball, float s) {
    if (how.x > 0.5 && how.x < 1.5) {
        // The plane sweeps from the far rim (collapse 0) to the near (1); what lies past it is reflected back
        // over it, so the far part lies folded on the near and the whole folds away.
        float h = ball * (1.0 - 2.0 * how.y);
        float t = dot(r, side);
        if (t > h) r -= 2.0 * (t - h) * side;
    }
    gGrowBirthS = s;
    gGrowBirthR = r;
    gGrowBirthBall = ball;
    gGrowBirthSide = side * ball;
    // Cave in: a hollow centred on the rim, up to twice the ball's radius, which eats the whole ball.
    gGrowBirthCave = how.x > 1.5 ? 2.0 * ball * how.y : 0.0;
}

/** A birth's scale: an imploding birth shrinks into its middle as it collapses. Never 0, so it divides. */
float growBirthScale(vec4 at, vec4 how) {
    return max(how.x < 0.5 ? at.w * (1.0 - how.y) : at.w, 1.0e-3);
}

/**
 * Where birth b reads the family's map for world point p, in a frame with eye `eye` and near depth `near`,
 * built round a subject at the origin (`around`: the read is in the subject's own space) or flown (the read
 * is the world round the birth's middle).
 */
vec3 growBirthIn(int b, vec3 p, vec3 eye, float near, bool around) {
    vec4 at = uBirths[2 * b];
    vec4 how = uBirths[2 * b + 1];
    float unit = 0.3 * near;
    vec3 o = at.xyz * unit;
    vec3 middle = around ? gGrowBasis * o : eye + gGrowBasis * (o + vec3(0.0, 0.0, GROW_BIRTH_AHEAD * unit));
    float s = growBirthScale(at, how);
    growBirthKeep((p - middle) / s, gGrowBasis * vec3(how.z, how.w, 0.0), how, GROW_BIRTH_BALL * unit, s);
    return (around ? vec3(0.0) : middle) + gGrowBirthR;
}

/** growBirthIn in lib_camera's frame (gGrowEye, gGrowNear, gGrowAround). */
vec3 growBirthAt(int b, vec3 p) {
    growFrame();
    return growBirthIn(b, p, gGrowEye, gGrowNear, gGrowAround);
}

/** The birth's distance, world units, from the map's distance d read where growBirthIn said: kept in its ball, hollowed as it caves in. */
float growBirthDist(float d) {
    float e = max(d, length(gGrowBirthR) - gGrowBirthBall);
    if (gGrowBirthCave > 0.0) e = max(e, gGrowBirthCave - length(gGrowBirthR - gGrowBirthSide));
    return e * gGrowBirthS;
}

/** How much of the birth is at the read point, 0..1: all of it inside its ball, soft at the rim, none in its hollow. */
float growBirthMask() {
    float m = 1.0 - smoothstep(0.8 * gGrowBirthBall, gGrowBirthBall, length(gGrowBirthR));
    if (gGrowBirthCave > 0.0) m *= smoothstep(0.0, 0.2 * gGrowBirthBall, length(gGrowBirthR - gGrowBirthSide) - gGrowBirthCave);
    return m;
}

/**
 * Birth b on a 2D pattern at uv, the pattern's frame round c with reach `reach` (the screen's middle and 1,
 * or the eye on the ground): where the pattern is read, the picture round c at the birth's scale. Lay it
 * over the pattern by growBirthMask.
 */
vec2 growBirth2(int b, vec2 uv, vec2 c, float reach) {
    vec4 at = uBirths[2 * b];
    vec4 how = uBirths[2 * b + 1];
    float unit = 0.3 * reach;
    float s = growBirthScale(at, how);
    growBirthKeep(vec3((uv - c - at.xy * unit) / s, 0.0), vec3(how.z, how.w, 0.0), how, GROW_BIRTH_BALL * unit, s);
    return c + gGrowBirthR.xy;
}

/**
 * Every birth at a screen point uv (aspect-corrected, y up, as view() returns it), for a style with no field
 * loop to run them in: inside each birth's ball the screen is drawn from the picture's middle, scaled down,
 * eased into the picture at the rim, so the style's one picture is read once. uv itself at Life 0.
 */
vec2 growBirths2(vec2 uv) {
    if (!(uLife > 0.0)) return uv;
    vec2 q = uv;
    for (int b = 0; b < 3; b++) {
        if (!growBorn(b)) continue;
        vec2 at = growBirth2(b, uv, vec2(0.0), 1.0);
        q = mix(q, at, growBirthMask());
    }
    return q;
}

/**
 * Every birth at a world point p, for a field read once a step with no passes to run them in (a volume
 * whose march reads its veils once a step): inside a living birth's ball the read moves to where that
 * birth reads the world, so the step's one read sees the birth there and the world everywhere else. p
 * itself at Life 0.
 */
vec3 growBirthsAt(vec3 p) {
    if (!(uLife > 0.0)) return p;
    for (int b = 0; b < 3; b++) {
        if (!growBorn(b)) continue;
        vec3 at = growBirthAt(b, p);
        if (growBirthMask() > 0.5) return at;
    }
    return p;
}

/**
 * How much farther a march shortcut must reach for the births, world units, in a frame of near depth `near`:
 * round a subject a birth's middle and ball lie within 3.2 birth units of it, flown within 6 of the eye.
 * Exactly 0.0 at Life 0.
 */
float growBirthSlack(float near, bool around) {
    if (!(uLife > 0.0)) return 0.0;
    return 0.3 * near * (around ? 3.2 : 6.0);
}

/**
 * How much farther than `extent` (a march shortcut's size: a bound's radius, a slab's half-height, a
 * give-up height) a library's grown world reaches, world units, for a library whose Spawn slides `span`
 * and whose Fractal reaches `reach` at full: the slide, the children and the layers' 18% (11% drawn in).
 * Life's births add their own reach (growBirthSlack). Exactly 0.0 while all four rest and Life is 0, so
 * `extent + slack` is `extent`, bit for bit.
 */
float growSlack(float extent, float span, float reach) {
    bool idle = growIdle();
    if (idle && !(uLife > 0.0)) return 0.0;
    growFrame();
    float born = growBirthSlack(gGrowNear, gGrowAround);
    if (idle) return born;
    float grown = (gGrowAround ? 0.3 * gGrowNear : span) * clamp(uSpawn, 0.0, 1.0) + reach * clamp(uFractal, 0.0, 1.0);
    return grown + 0.18 * max(abs(uFrontLayer), abs(uBackLayer)) * (abs(extent) + grown) + born;
}

/**
 * Spawn's copy: how far it has slid at world point offset. A flown world slides along `axis` (unit),
 * `span` world units at full Spawn, so the copy lands between the first's cells and the way stays
 * open; a subject at the origin moves aside (+x) by about its own size, so the twin stands beside it.
 */
vec3 growSpawnShift(vec3 axis, float span) {
    return gGrowAround ? vec3(0.3 * gGrowNear * uSpawn, 0.0, 0.0) : axis * (span * uSpawn);
}

/**
 * Fractal's child space at world point q: the family's world `scale` times smaller (evaluate the map
 * there and divide its distance by `scale`), repeated every `cell` world units and set half a cell off
 * the parent, so a style built round one subject at the origin grows copies all over its surface
 * rather than one hidden inside it.
 */
vec3 growChildSpace(vec3 q, float scale, float cell) {
    vec3 r = q + 0.5 * cell;
    r -= cell * floor(r / cell + 0.5);
    return r * scale;
}

/** Fractal's shell: how far the child copies reach out of the subject's surface, `reach` world units at full Fractal. */
float growFractalReach(float reach) {
    return reach * uFractal;
}

/**
 * Fractal's child at the parent's distance `parent`: the child copy's distance `child` (already
 * divided back to world units), kept within the shell. min(parent, this) is the grown subject:
 * at a reach of 0 it is the parent itself.
 */
float growFractalChild(float child, float parent, float reach) {
    return max(child, parent - reach);
}

/**
 * A grown copy's distance `d` at world point p, kept `clear` world units off the eye: a slid copy or a
 * child must never close round the eye, where a style keeps its own open air.
 */
float growClearEye(float d, vec3 p, float clear) {
    return max(d, clear - length(p - gGrowEye));
}

/** 0 for the front layer at the eye, 1 for the back layer from twice gGrowNear on. */
float growDepth(vec3 p) {
    return smoothstep(0.5 * gGrowNear, 2.0 * gGrowNear, length(p - gGrowEye));
}

/**
 * How far the layer that holds world point p opens: a scale, 1 + `amp` at full Front (or Back) layer,
 * 1 - 0.6 `amp` drawn in all the way, a little uneven over space so it reads as breathing. Exactly 1
 * while both layers are at 0. Hand it to growOpened, and multiply the map's distance by it.
 */
float growOpen(vec3 p, float amp) {
    if (uFrontLayer == 0.0 && uBackLayer == 0.0) return 1.0;
    float layer = mix(uFrontLayer, uBackLayer, growDepth(p));
    layer *= layer < 0.0 ? 0.6 : 1.0;
    return 1.0 + amp * layer * (0.9 + 0.1 * sin(dot(p, vec3(2.3, 1.7, 2.9))));
}

/** q (where the style's map is read) opened by k: about the axis the eye rides, or about the subject. */
vec3 growOpened(vec3 q, float k) {
    if (k == 1.0) return q;
    return gGrowAround ? q / k : vec3(q.xy / k, q.z);
}

/**
 * One layer's swell of a surface, world units, for a family layer that is a real surface of its own
 * (a skin, a veil): `layer` is uFrontLayer or uBackLayer. Subtract it from that surface's distance.
 * Exactly 0 while the layer is at 0.
 */
float growSwellOf(float layer, vec3 p, float amp) {
    if (layer == 0.0) return 0.0;
    layer *= layer < 0.0 ? 0.6 : 1.0;
    return amp * layer * (0.7 + 0.3 * sin(dot(p, vec3(2.3, 1.7, 2.9))));
}

/**
 * A backdrop's direction (or position) d bent by Back layer: the far field opens and draws in, a
 * swell that runs across it, at most `amp`. d itself while Back layer is 0.
 */
vec3 growBackdrop(vec3 d, float amp) {
    if (uBackLayer == 0.0) return d;
    return d + amp * uBackLayer * sin(d.yzx * 2.6 + vec3(0.0, 1.7, 3.1));
}

/** A flat backdrop's position bent by Back layer, as growBackdrop. */
vec2 growBackdrop2(vec2 d, float amp) {
    if (uBackLayer == 0.0) return d;
    return d + amp * uBackLayer * sin(d.yx * 2.6 + vec2(0.0, 1.7));
}
