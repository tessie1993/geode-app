#pragma once

#include <cstdint>

#include "scene/movement/Body.hpp"

// The shape changes: morph (one shape into another over a duration) and collapse (shrink to nothing
// with a twist). Both are a clock and an ease curve, so they are exact at every frame rate: the
// progress is a function of the elapsed time alone. The object (#515) reads a morph's from, to and
// progress for its SDF blend; collapse writes the body's scale and orientation itself.

namespace geode::scene::movement {

// The ease curves a shape change can take, by the Int a param stores. Every curve runs 0 → 1 and never
// leaves 0..1 (a morph's blend past either end means nothing).
enum class Ease : int {
    Linear = 0,
    Smooth = 1,     // smoothstep: 3t² - 2t³
    InOutCubic = 2,
    InCubic = 3,    // slow start, fast end: an implosion gathering pace
    OutCubic = 4,   // fast start, slow end
};
constexpr int kEaseCount = 5;

float ease(Ease curve, float t);

// A clock's elapsed time within this much of its duration counts as done, so a duration that is a whole
// number of frames ends on that frame although float sums of dt fall a hair short.
constexpr float kTimeSlack = 1e-4f;

// ---- Morph ------------------------------------------------------------------------------------------
struct MorphParams {
    float duration = 1.5f;  // seconds; 0 changes at once
    int ease = static_cast<int>(Ease::Smooth);
    bool operator==(const MorphParams&) const = default;
};

struct MorphState {
    std::uint16_t from = 0;
    std::uint16_t to = 0;
    float elapsed = 0.0f;
    float linear = 1.0f;    // elapsed / duration, 0..1
    float progress = 1.0f;  // ease(linear): the blend from `from` (0) to `to` (1)
    bool running() const { return linear < 1.0f; }
};

// Starts a morph to `to` from the shape shown most now (`to` once the blend is past half, else `from`),
// so a new morph never starts from a shape that was barely visible. Morphing to the shape already shown
// at rest does nothing.
void beginMorph(MorphState& s, std::uint16_t to);
void stepMorph(MorphState& s, const MorphParams& p, float dt);

// ---- Collapse ---------------------------------------------------------------------------------------
// The body's scale falls from what it was at the start to exactly 0 at `duration`, along the ease
// curve, while it turns `twist` radians in all about its own up axis (the twist follows the same eased
// progress, so it spins fastest as it vanishes under InCubic). At the end the body is dead unless
// `kill` is off.
struct CollapseParams {
    float duration = 0.8f;
    float twist = 6.2831853f;  // radians over the whole collapse
    int ease = static_cast<int>(Ease::InCubic);
    bool kill = true;
    bool operator==(const CollapseParams&) const = default;
};

struct CollapseState {
    bool active = false;
    bool done = false;
    float elapsed = 0.0f;
    float startScale = 1.0f;
    float applied = 0.0f;  // the eased progress already turned into twist
};

void beginCollapse(CollapseState& s, const Body& b);
void stepCollapse(Body& b, CollapseState& s, const CollapseParams& p, float dt);

}  // namespace geode::scene::movement
