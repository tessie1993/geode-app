#pragma once
#include <array>
#include <cstdint>

namespace geode::viz::shapes {

// The shape graph: a small, morphing scene of up to eight primitives that a raymarched style can melt into
// its own form (Customize › Shape › Grow › Shapes; lib_shapes.glsl draws it, uShapeMix is how much).
//
// What it is. A flat list of shapes, each a primitive (ball, block, rounded block, ring, capsule, diamond,
// drum) with a place, a size, a colour index (0..1 along the style's OWN palette, never a colour of its own)
// and an operation onto what came before it: union, smooth blend, cut (smooth subtract) or mask (smooth
// intersect), with a blend radius. The list is a CSG tree one level deep: a head shape opens a group, the
// children after it combine onto that group first, and the finished group lands on the scene by the head's
// operation. Within a group the adding children come before the carving and masking ones, and the groups
// whose head adds come before the groups whose head carves, so a cut is never filled back in by a later union.
//
// Where it comes from. Its target is a pure function of Inputs: the play's seed, and the scene params Spawn
// (how many shapes), Morph (how soft every blend is), Formula (which primitives and which operations: four
// families of mixes along 0..1), Fractal (how far the whole graph is folded into an angular repeat about the
// vertical axis, and how many folds) and Shapes (how far the shapes spread from the middle). No sound field,
// no tempo and no beat reaches it: the music moves it only through those params, which the reaction matrix,
// the waves, the envelopes, the macro knobs and the Auto Director move.
//
// How it moves. A changed input picks a new target; what is shown morphs to it, so nothing ever pops:
// - every number (place, size, colour index, blend radius, fold amount, melt radius) glides toward its target
//   with a time constant of kGlideSeconds of wall time (Slider glides keep wall time, as the resolver's do);
// - a primitive or operation change crossfades between the old and the new result by a weight that runs
//   linearly over kCrossfadeSeconds (the shader mixes the two distances, a convex blend of two distance
//   bounds that stays 1-Lipschitz); a change asked mid-crossfade back to the old one turns the weight around,
//   and one to a third waits until the running crossfade lands, so only two ever mix;
// - a shape that joins grows from size 0 and one that leaves shrinks to 0 on the same linear ramp, its
//   operation weighted by the same presence, so its blend into its neighbours swells and fades with it;
// - the fold's count crossfades like a primitive, between two folded copies of space (one evaluation of the
//   graph: the folded points are mixed, not the graph twice).
// Between changes a slow drift keeps it alive: each head wanders by a few hundredths around its place, on
// phases advanced by its own clock (Speed times the music's frame step, the uTime rule, so no sound means no
// drift), as translations only. Nothing in the graph turns, and nothing turns to the music.
//
// Allocation-free and deterministic: the same seed and the same (Inputs, dt) sequence give the same floats,
// so an export that ticks on its own frame step replays itself. Host-tested in core/tests/shape_graph_test.

inline constexpr int kMaxShapes = 8;
// Four vec4 per shape in uShapes (lib_scene_uniforms):
//   [0] xyz place (drift included), w role: 0 a head that opens a group, 1 a child of the open group
//   [1] xyz size (each primitive reads its own components; already times the shape's presence), w colour index
//   [2] x primitive A, y primitive B, z crossfade A→B (0..1), w presence (0 absent .. 1 whole)
//   [3] x operation A, y operation B, z crossfade A→B (0..1), w blend radius
inline constexpr int kVec4PerShape = 4;
inline constexpr int kShapeFloats = kMaxShapes * kVec4PerShape * 4;

// The primitives, by the index the shader switches on (lib_shapes' shapePrim). Sizes per primitive:
//   Ball: x radius. Block: xyz half extents. RoundBlock: xyz half extents, rounded by a quarter of the least.
//   Ring: x the ring's radius, y the tube's. Capsule: x radius, y half length (along y). Diamond: x the
//   radius to a tip. Drum: x radius, y half height (along y).
enum class Prim : int { Ball, Block, RoundBlock, Ring, Capsule, Diamond, Drum };
inline constexpr int kPrimCount = 7;
// The operations onto what came before, by the shader's index (lib_shapes' shapeOp).
enum class Op : int { Union, Blend, Cut, Mask };
inline constexpr int kOpCount = 4;

// The fewest and most folds the graph is folded into about the vertical axis (Fractal).
inline constexpr int kMinFolds = 2;
inline constexpr int kMaxFolds = 6;

inline constexpr float kGlideSeconds = 0.7f;
inline constexpr float kCrossfadeSeconds = 0.9f;
// A head's drift: how far it wanders from its place, and how fast its three phases run at Speed 1 (radians a
// second, the lowest and the highest).
inline constexpr float kDriftReach = 0.05f;
inline constexpr float kDriftRateLo = 0.13f;
inline constexpr float kDriftRateHi = 0.41f;

struct Inputs {
    uint64_t seed = 0;
    float spawn = 0.0f;
    float morph = 0.0f;
    float formula = 0.0f;
    float fractal = 0.0f;
    float shapes = 0.0f;
    float speed = 1.0f;
};

// One shape of a target, in evaluation order.
struct SlotTarget {
    bool alive = false;
    bool head = true;
    Prim prim = Prim::Ball;
    Op op = Op::Union;
    std::array<float, 3> place{};
    std::array<float, 3> size{};
    float colour = 0.0f;
    float radius = 0.0f;
};

struct Target {
    std::array<SlotTarget, kMaxShapes> slots{};
    int alive = 0;
    int folds = kMinFolds;
    float foldAmount = 0.0f;
    float melt = 0.0f;
};

// The graph the inputs ask for: a pure function, the same inputs giving the same target. Out-of-range or
// non-finite inputs read as their nearest sane value (0 for a NaN).
Target targetOf(const Inputs& in);

// What the renderer uploads: uShapes (kShapeFloats), uShapeCount, uShapeMix, uShapeFold, uShapeMelt.
struct Packed {
    std::array<float, kShapeFloats> shapes{};
    // How many entries of shapes the shader walks: one past the last present shape in evaluation order.
    int count = 0;
    // Shapes, how far a style melts the graph into its form: exactly the param, 0 = the style untouched.
    float mix = 0.0f;
    // x folds A, y folds B, z crossfade A→B, w how far space is folded (0 none).
    std::array<float, 4> fold{};
    // The blend radius the graph melts into the style's own distance with.
    float melt = 0.0f;
};

class ShapeGraph {
public:
    // Forget everything: the next tick shows its target at once, as a fresh renderer's first frame does.
    void reset();
    // One frame: glideDt is the wall step every glide and crossfade runs on, clockDt the music's frame step
    // the drift's clock runs on (times Speed). Negative or non-finite steps count as 0.
    void tick(const Inputs& in, float glideDt, float clockDt);
    const Packed& packed() const { return packed_; }

private:
    struct Crossfade {
        int from = 0;
        int to = 0;
        float w = 1.0f;
        void snap(int target);
        void step(int target, float amount);
    };
    struct Slot {
        bool head = true;
        int headIndex = 0;
        std::array<float, 3> place{};
        std::array<float, 3> size{};
        float colour = 0.0f;
        float radius = 0.0f;
        float presence = 0.0f;
        Crossfade prim;
        Crossfade op;
        std::array<float, 3> phase{};
        std::array<float, 3> rate{};
    };

    void pack();

    std::array<Slot, kMaxShapes> slots_{};
    Crossfade folds_;
    float foldAmount_ = 0.0f;
    float melt_ = 0.0f;
    float mix_ = 0.0f;
    uint64_t seed_ = 0;
    bool primed_ = false;
    Packed packed_{};
};

// The graph's distance and colour at a point, exactly as lib_shapes' shapesScene computes them (the host
// tests' window on the shader): distance first, colour index second. Far from everything (or with no shape
// present) the distance is kFar.
inline constexpr float kFar = 1000.0f;
struct Sample {
    float distance = kFar;
    float colour = 0.0f;
};
Sample sample(const Packed& g, float x, float y, float z);
// lib_shapes' shapesInto: the graph melted into a style's own distance d and colour index by g.mix.
Sample sampleInto(const Packed& g, float d, float colour, float x, float y, float z);

}  // namespace geode::viz::shapes
