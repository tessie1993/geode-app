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

/**
 * How much farther than `extent` (a march shortcut's size: a bound's radius, a slab's half-height, a
 * give-up height) a library's grown world reaches, world units, for a library whose Spawn slides `span`
 * and whose Fractal reaches `reach` at full: the slide, the children and the layers' 18% (11% drawn in).
 * Exactly 0.0 while all four rest, so `extent + slack` is `extent`, bit for bit.
 */
float growSlack(float extent, float span, float reach) {
    if (growIdle()) return 0.0;
    growFrame();
    float grown = (gGrowAround ? 0.3 * gGrowNear : span) * clamp(uSpawn, 0.0, 1.0) + reach * clamp(uFractal, 0.0, 1.0);
    return grown + 0.18 * max(abs(uFrontLayer), abs(uBackLayer)) * (abs(extent) + grown);
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
