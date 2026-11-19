#pragma once
#include <array>
#include <cstdint>

#include "sound/SoundFrame.h"
#include "viz/CameraRig.hpp"
#include "viz/NoteResponse.hpp"
#include "viz/ShapeGraph.hpp"

// Holding a shader style's last picture on a still frame (the owner, 2026-11-18). A shader style is marched
// whole every frame from its params, its clocks and what it reads; with the music paused or silent the clock
// stops (dt 0), and a frame drawn from the same inputs is the same picture. The renderer then keeps the scene
// target as it is instead of marching it again; the composite and everything after it still run every frame.
//
// The renderer's frameStill_ already covers the clock (dt 0), the params wire, the render size, a transition in
// progress and the redraws a change owes (a palette, a new fragment source, a scene's first frames). This adds
// everything else a ShaderScene's draw reads. When in doubt it redraws: a stale frame is worse than a wasted one.
// Pure, no GL: core/tests/shader_hold_test.cpp.
namespace geode::viz::shader_hold {

// TouchField's points (kMaxPoints * kPointStride) and its anchor, axis, spin, gesture and count, in that order.
inline constexpr int kTouchFloats = 32;

struct Inputs {
    bool still = false;      // the renderer's frameStill_
    bool exporting = false;  // an export never holds
    bool newPcm = false;     // samples arrived for the scene this frame (its waveform)
    GeodeSoundFrame sound{};  // the frame the style hears, its band trims applied
    std::array<float, kTouchFloats> touch{};
    CameraPose pose{};
    NoteUniforms notes{};      // uKnob
    shapes::Packed shapes{};   // the shape graph, which glides on the wall clock
    float marchScale = 1.0f;   // Settings › Performance's march budget
    uint64_t playSeed = 0;     // Morph World's worlds
    uint64_t overrides = 0;    // the user's shader edits (shader_overrides::generation)
    uint32_t paletteRamp = 0;  // the palette stack's texture, 0 without one
};

// Every field equal; the sound frame bit for bit.
bool same(const Inputs& a, const Inputs& b);

// One scene's record of the inputs it last drew from.
class Held {
public:
    // Still draws in a row from the same inputs before the picture may stand, so whatever the scene's update
    // moves on a repeated input (an edge it consumes, a glide toward a target) has settled into the held frame.
    static constexpr int kSettleDraws = 2;

    // True when the picture drawn last may stand for a frame of `now`.
    bool holds(const Inputs& now) const;
    // The scene was drawn from `now`.
    void drew(const Inputs& now);

private:
    Inputs last_{};
    bool any_ = false;
    int settled_ = 0;  // still draws in a row from last_
};

}  // namespace geode::viz::shader_hold
