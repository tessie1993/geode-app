#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Morph World's genome: one fixed-size vector of floats that says what the world is, so one
// shader (morph_world_frag, on lib_world) can be any of many worlds and slide between them.
//
// WHY A GENOME. A morphing 3D environment works because its space is a CHAIN of continuous domain
// operators (repeat, mirror fold, a rotate-fold IFS step, sphere inversion, abs-inversion, twist,
// scale) ending in a simple primitive. Move the operators' parameters continuously and the shape
// changes with nothing swapped. Every operator here therefore has a strength that fades it from the
// identity (0, off) to full (1), so any genome can be lerped into any other without a pop: a beam
// scaffold slides into a foam of spheres by every field easing at once.
//
// THE CHAIN, in lib_world's order (world() there; sdf() below is the same function on the CPU):
//   z repeat (always on: the travel axis is endless), x/y repeat (RepXY fades it, CellShift moves
//   the cells off the axis, so the way runs between structures rather than through them), a polar
//   kaleidoscope fold about the travel axis (N-fold, round tunnels, a spiral), twist along
//   travel, global scale, mirror fold, up to five IFS steps (rotate-fold, fractional count),
//   sphere inversion, abs-inversion, then thirteen primitives blended by weight (six simple ones, a
//   baked CC0 model from WorldAssets, and six fractals: gyroid, Menger sponge, Mandelbulb, Mandelbox,
//   Sierpinski tetrahedron, Apollonian packing), a ground plane joined under them, then the corridor
//   carved out around the camera's way. Three render modes read that one world: a hard surface,
//   a glow-through volume (translucent glowing plates) and an emissive density (thin glowing
//   sheets); their weights are in the genome too, so a world can melt from stone into light.
//
// WHAT MUST HOLD (valid()): every field inside its bounds, the primitive weights and the mode
// weights each summing to one, and a world that is neither solid nor empty: a probe of the space
// near the camera's way finds both open air and matter (openSpace(); a ground and the rock under
// it are not the camera's space, so the probe skips them). random() only returns
// genomes that pass, so nothing the morph driver picks can trap the camera in rock or show an
// empty black screen.
//
// Pure: no GL, no allocation, no clock. The seed genomes are Geode's own, designed for this file.
namespace geode::viz::world {

// Every genome field, in upload order: uWorld[i / 4][i % 4] in lib_world (its WG_* names).
enum Field : int {
    // Domain operators.
    RepXY = 0,   // 0..1: x/y cell repeat strength (0 = one column of cells along travel)
    CellXY,      // x/y cell size, world units
    CellZ,       // the travel axis' cell, world units (always repeating, mirrored: the period is twice this)
    CellShift,   // 0..1: how far the x/y cells sit off the travel axis (1 = the axis runs between them)
    Mirror,      // 0..1: mirror fold strength
    MirrorOff,   // the fold's offset from the plane
    Ifs,         // 0..1: rotate-fold IFS strength
    IfsIter,     // 1..5 IFS steps, fractional (the last step fades in)
    IfsScale,    // each step's scale
    IfsAngA,     // radians, the xy turn of each step
    IfsAngB,     // radians, the yz turn of each step
    IfsOffX,     // each step's offset
    IfsOffY,
    IfsOffZ,
    Invert,      // 0..1: sphere inversion strength
    InvRadius,   // the inversion sphere's radius
    AbsInv,      // 0..1: abs-inversion strength (abs(p) / |p|^2 - c)
    AbsInvC,     // its constant
    Twist,       // radians: the xy turn along travel, a sine over one z period
    Polar,       // 0..1: kaleidoscope fold around the travel axis (round, N-fold tunnels, not the 4-way cross)
    PolarN,      // 3..12 segments, fractional = a blend of the two neighbouring counts
    PolarTwist,  // radians per world unit: the fold turns along travel, a spiral
    Scale,       // the whole world's scale
    // Primitives: weights (normalized to sum 1), then their size and wall thickness.
    PBox,
    PBar,
    PTube,
    PTorus,
    PShell,
    PSlab,
    PMesh,       // a baked model (WorldAssets), sized by MeshSize
    PGyroid,     // a gyroid sheet: one endless, smooth, organic surface (GyroScale, PrimThick)
    PMenger,     // a Menger sponge, three levels (PrimSize)
    PBulb,       // a Mandelbulb (BulbPower, PrimSize)
    PMandelbox,  // a Mandelbox: box fold, sphere fold, scale (BoxScale, PrimSize)
    PSierp,      // a Sierpinski tetrahedron, folded seven times (PrimSize)
    PApollo,     // an Apollonian packing: spheres inverted into spheres, endless (PrimSize)
    PrimSize,
    PrimThick,
    MeshPick,    // 0..kShapeCount-1: which baked model, fractional = a blend of two neighbours
    MeshSize,    // the model's half-size, world units before Scale
    BulbPower,   // the Mandelbulb's power, 2..9
    GyroScale,   // the gyroid's frequency
    BoxScale,    // the Mandelbox's scale, 1.5..3 (applied negative: the folded, cathedral-like box)
    // Render modes: weights (normalized to sum 1), then the density mode's band half-width.
    MSurface,
    MGlow,
    MDensity,
    DensBand,
    // Colour: how much each orbit trap channel moves the palette, the palette offset (wraps) and span.
    TrapX,
    TrapY,
    TrapZ,
    TrapR,
    PalOffset,
    PalSpan,
    // Light and air.
    Emission,
    Fog,
    Corridor,    // the radius of the open tube along the travel axis
    // Surface detail from the baked material maps (WorldAssets), grey so colour stays pal()'s.
    TexAmount,   // 0..1: how much the detail varies the surface's albedo
    TexPick,     // 0..kTexCount-1: which map, fractional = a blend of two neighbours
    TexScale,    // repeats per world unit
    TexEmit,     // 0..1: how much the detail patterns the light (rims, glow, density)
    // The camera's feel in this world: the view only (the eye and its corridor never move).
    Roll,        // radians per second the view banks round the way (signed): a slow barrel roll
    Lens,        // the focal length's factor: under 1 a wide lens (vast halls), over 1 a long one (close detail)
    // The ground and the sky (#494): a plane joined to the world, and the air an escaped ray shows.
    Floor,        // 0..1: a ground plane under the world, joined smoothly and faded in (0 = none)
    FloorY,       // the ground's height, world units (under the deepest eye of every rig path, bubble included)
    FloorGloss,   // 0..1: how mirror-like the ground is (a fresnel-weighted reflected march)
    Sky,          // 0..1: a sky gradient where rays escape and fog fades (0 = black space)
    SkyHue,       // the sky's palette coordinate
    Horizon,      // 0..1: the glow band where sky meets ground, and so the colour far fog fades toward
    // Seams for the next worlds (identity until their ticket lands): landscape, lattice, gaze.
    Terrain,      // 0..1: a fractal landscape layer on the ground (ridged height field)
    TerrainHeight, // the landscape's relief, world units
    TerrainScale, // the landscape's base frequency, per world unit
    LatticeTilt,  // radians: the x/y repeat lattice tipped about x (breaks the 4-way cross)
    LatticeYaw,   // radians: the lattice turned about y
    GazePitch,    // radians: how far the view may look up and down (view only)
    GazeYaw,      // radians: how far the view may look to the sides (view only)
    kFieldCount,
};

// The runtime values the driver writes after the genome (never lerped, never mutated).
enum Runtime : int {
    ZPhase = kFieldCount,  // the z offset (world units, 0..2 CellZ) that keeps the travel seamless as CellZ morphs
    Gain,                  // the smoothed brightness envelope (retargets never flash)
    IterCap,               // Detail's cap on IFS steps, 1..5
    HueTurn,               // the chord's turn of the palette, added on top of PalOffset
    WakeX,                 // where the camera is heading, times how far: the corridor's wake
    WakeY,
    WakeZ,
    Assets,                // 1 when the baked shapes and maps are bound, else 0 (mesh = sphere, no detail)
    RollAngle,             // the view's bank, radians: Roll integrated, a slow sway, a lean into sideways motion
    LensNow,               // Lens with the kick's punch on top: the factor lib_world's ray takes on uCamFocal
    GazeX,                  // the view's look to the side now, radians (the driver's, within GazeYaw)
    GazeY,                  // the view's look up or down now, radians (within GazePitch)
    Spare,                  // unused: pads uWorld to whole vec4s
    kUniformCount,
};

constexpr int kVec4 = kUniformCount / 4;
static_assert(kUniformCount % 4 == 0, "uWorld is whole vec4s");
static_assert(kVec4 == 22, "lib_world declares uWorld[22]");

constexpr int kMaxIfsSteps = 5;
// The corridor's eye bubble (lib_world's WG_EYE_RADIUS): what is always open around the eye.
constexpr float kEyeRadius = 0.9f;

struct Bounds {
    float lo;
    float hi;
};
Bounds bounds(int field);
// PalOffset wraps around the colour wheel instead of clamping.
bool wraps(int field);

struct Genome {
    std::array<float, kFieldCount> v{};
    float& operator[](int i) { return v[static_cast<std::size_t>(i)]; }
    float operator[](int i) const { return v[static_cast<std::size_t>(i)]; }
};

// The world blended a -> b. t <= 0 returns a exactly and t >= 1 returns b exactly. Between, each
// field eases by its group: the space's operators on a smootherstep (they start and land with no
// speed), the colours linearly (the palette should not hesitate), the palette offset the short way
// round the wheel. Weights are lerped with one easing per group, so they keep summing to one.
Genome lerp(const Genome& a, const Genome& b, float t);

// Every field into its bounds, the weights normalized; never touches open space.
Genome clampFields(Genome g);
bool inBounds(const Genome& g);

// The camera's way, as world() needs it beside the genome: the z phase and the wake.
struct Way {
    float zPhase = 0.0f;
    std::array<float, 3> eye{0.0f, 0.0f, 0.0f};
    std::array<float, 3> wake{0.0f, 0.0f, 0.0f};
    float eyeRadius = kEyeRadius;
    int iterCap = kMaxIfsSteps;
};

// lib_world's world(p), ported line for line: the signed distance at world point p, the corridor
// carved. Positive outside matter. The orbit trap is dropped; tests only need the distance.
float sdf(const Genome& g, const Way& way, const std::array<float, 3>& p);
// The same without the corridor: the bare world, for the open-space probe.
float sdfUncarved(const Genome& g, const Way& way, const std::array<float, 3>& p);

// The share of probe points around the camera's way (a 12-unit box about the travel axis, eye at
// the origin) that are open air (> 0.25 from matter) and that are near matter (< 0.6). A good
// world has both: open in every direction, but with something to see. With a floor, points under
// it and in the band its surface reaches are left out of both shares.
struct Space {
    float open = 0.0f;
    float near = 0.0f;
};
Space openSpace(const Genome& g);
constexpr float kMinOpen = 0.35f;
constexpr float kMinNear = 0.06f;
bool valid(const Genome& g);

// ---- the seed worlds ----------------------------------------------------------
//
// Hand-designed, named by us. Each has a character the music picks it by: how much energy it
// carries, how bright its mood is, how dense the space feels (each 0..1).
struct Seed {
    const char* name;
    float energy;
    float mood;
    float density;
    Genome genome;
};
int seedCount();
const Seed& seed(int index);

// What the music asks of the next world.
struct Constraints {
    float mutation = 0.25f;  // 0 = a seed as designed .. 1 = as far as bounds allow
    float energy = 0.5f;
    float mood = 0.5f;
    float density = 0.5f;
    int avoid = -1;          // the seed the world is in now: picked far less often
};

// A valid genome for this seed and these wants: a seed drawn by closeness to (energy, mood,
// density), then every field mutated by up to `mutation` of its range, renormalized and checked;
// a mutation that leaves no open space or nothing to see is pulled back toward the seed until it
// passes (the seed itself always passes). The same inputs give the same genome. `picked` gets the
// seed index it grew from.
Genome random(uint64_t seed, const Constraints& c, int* picked = nullptr);

// A rough brightness the genome renders at (emission by mode), for the gain envelope.
float brightness(const Genome& g);

// splitmix64, the one generator: tests and the driver must agree on it.
uint64_t mix64(uint64_t x);

}  // namespace geode::viz::world
