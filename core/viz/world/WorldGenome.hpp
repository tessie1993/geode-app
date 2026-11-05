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
//   sphere inversion, abs-inversion, then ten primitives blended by weight (six simple ones, a baked
//   CC0 model from WorldAssets, a gyroid, a Menger sponge and a Mandelbulb), then the corridor
//   carved out around the camera's way. Three render modes read that one world: a hard surface,
//   a glow-through volume (translucent glowing plates) and an emissive density (thin glowing
//   sheets); their weights are in the genome too, so a world can melt from stone into light.
//
// WHAT MUST HOLD (valid()): every field inside its bounds, the primitive weights and the mode
// weights each summing to one, and a world that is neither solid nor empty: a probe of the space
// near the camera's way finds both open air and matter (openSpace()). random() only returns
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
    PrimSize,
    PrimThick,
    MeshPick,    // 0..kShapeCount-1: which baked model, fractional = a blend of two neighbours
    MeshSize,    // the model's half-size, world units before Scale
    BulbPower,   // the Mandelbulb's power, 2..9
    GyroScale,   // the gyroid's frequency
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
    kFieldCount,
};

// Four runtime values the driver writes after the genome (never lerped, never mutated).
enum Runtime : int {
    ZPhase = kFieldCount,  // the z offset (world units, 0..2 CellZ) that keeps the travel seamless as CellZ morphs
    Gain,                  // the smoothed brightness envelope (retargets never flash)
    IterCap,               // Detail's cap on IFS steps, 1..5
    HueTurn,               // the chord's turn of the palette, added on top of PalOffset
    WakeX,                 // where the camera is heading, times how far: the corridor's wake
    WakeY,
    WakeZ,
    Assets,                // 1 when the baked shapes and maps are bound, else 0 (mesh = sphere, no detail)
    kUniformCount,
};

constexpr int kVec4 = kUniformCount / 4;
static_assert(kUniformCount % 4 == 0, "uWorld is whole vec4s");
static_assert(kVec4 == 16, "lib_world declares uWorld[16]");

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
// world has both: open in every direction, but with something to see.
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
