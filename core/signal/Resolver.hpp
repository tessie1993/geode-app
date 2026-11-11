#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "signal/EngineSettings.hpp"
#include "sound/SoundFrame.h"
#include "viz/Adsr.hpp"
#include "viz/CameraRig.hpp"
#include "viz/Director.hpp"
#include "viz/Lfo.hpp"
#include "viz/Macros.hpp"
#include "viz/MotionField.hpp"
#include "viz/NoteResponse.hpp"
#include "viz/Params.hpp"
#include "viz/Reactions.hpp"

namespace geode::signal {

// The scene params resolver (GLOSSARY): the engine's per-frame chain that turns the params the shell asked
// for into the ones a scene is drawn with. One step a frame, in this order, and nowhere else:
//
//   param fade (the style's own paramFadeSec, or a preset morph)
//   → auto-director (its look lies under everything after it)
//   → Note & key reactions (inside the director's look: its hue anchors the notes' colours)
//   → macros (the user's knobs with the director's laid over them)
//   → LFOs → envelopes
//   → reaction routes (the user's or the style's DNA) → macro routes → director routes
//   → motion field → safety::apply, last, so nothing before it can pass the flash and motion limits
//
// WHY a module of its own (#344): the chain used to sit inside the GL renderer, behind its mutex and
// atomics, reading the process-wide macros, Note & key switch and hand mask live. The order was only
// visible on a device, and an export heard live edits. Here it knows nothing of GL or threads: everything
// it reads arrives as a plain value (EngineSettings, StyleContext, the sound frame, dt), so the order is
// host-tested in one place (core/tests/resolver_test.cpp), two resolvers given the same inputs give the
// same bits, and every renderer (live, wallpaper, dream, export) runs the same chain.
//
// Single-threaded: no mutex, no atomics. The renderer keeps its lock and atomics and hands the resolver
// copies at its frame latch. Device policy (thermal tier, post-FX, Liquid's hot policy), the camera rig
// and the post pass's integrators stay in the renderer, after the step.
//
// It depends on core/sound (the frame) and on the GL-free stage code still in core/viz (Director,
// NoteResponse, Lfo, Adsr, Reactions, Macros, MotionField, VisualSafety, Catalogue); never on a GL header.
// ADR 0006's later steps (route producers, base plus modulation, one clamp) rebuild behind this interface.

// The chain reads one EngineSettings value a frame (signal/EngineSettings.hpp): the renderer's SettingsLatch
// hands it the process globals live, or an export's copy pinned at prepare (#350).

// The style on screen this frame.
struct StyleContext {
    const std::string* id = nullptr;  // null when no style is on screen
    // How the style reads paletteBase: true for the shader family (lib_palette's pal() runs the colour wheel
    // backwards), and when nothing is on screen; false for MilkDrop's tint and the simulations (a plain hue).
    bool shaderFamily = true;
    float spin = 0.0f;                // SceneRegistry::spinRate of the requested style
    // A new style came on screen since the last step: the motion field takes that style's variant.
    bool switched = false;
};

class Resolver {
public:
    struct Frame {
        // The params a scene is drawn with, before the renderer's device masks.
        viz::SceneParams params;
        // The director's style moments this step: a look committed at a new section or a drop (Auto
        // changes style on each).
        int styleMoments = 0;
        // The camera rig's music moves: the macros' Space and Speed under the director's look.
        viz::CameraRig::Moves cameraMoves{};
    };

    // Per-renderer configuration, kept until changed. Plain copies; call from the stepping thread.
    void setLfoConfigs(const std::array<viz::LfoConfig, viz::LfoEngine::kSlots>& configs) { lfo_.configs = configs; }
    void setAdsrConfigs(const std::array<viz::AdsrConfig, viz::AdsrEngine::kCount>& configs) { adsr_.configs = configs; }
    void setReactionRoutes(const std::array<viz::ReactRoute, viz::ReactionEngine::kRoutes>& routes) { reactions_.routes = routes; }
    // A preset morph: the params glide over `seconds`, for three times that long. <= 0 is ignored.
    void beginMorph(float seconds);
    // A new surface: the stages' state starts over (their configuration and the fade are kept).
    void reset();

    // One frame.
    const Frame& step(const viz::SceneParams& requested, const GeodeSoundFrame& sound, float dt,
                      const EngineSettings& settings, const StyleContext& style,
                      const sound::Emotion::State* heard = nullptr, float fadeDt = -1.0f);

    // The last step's result.
    const Frame& frame() const { return frame_; }
    // The note knobs, hues and tint the last step published; the scenes and the composite read them.
    // The reference stays valid for the resolver's life.
    const viz::NoteUniforms& noteUniforms() const { return notes_.uniforms(); }

    const viz::Director& director() const { return director_; }
    const viz::NoteResponse& notes() const { return notes_; }
    const viz::MotionField& motionField() const { return motionField_; }

private:
    viz::LfoEngine lfo_;
    viz::AdsrEngine adsr_;
    viz::ReactionEngine reactions_;
    // The macros' own routes, in an engine of their own so they never take a user slot.
    viz::ReactionEngine macroReactions_;
    viz::Director director_;
    int directorCommitsSeen_ = 0;
    viz::NoteResponse notes_;
    // The one stage every family's parameters pass through, fed the sound frame.
    viz::MotionField motionField_;

    // The faded params, before the director; what the next frame fades from.
    viz::SceneParams displayed_;
    float morphFadeSec_ = 0.0f;
    float morphRemainSec_ = 0.0f;
    // The user's knobs with the director's laid over them, this frame; what the params and the camera read.
    viz::MacroKnobs directedKnobs_{};
    std::array<float, viz::LfoEngine::kSlots> envRate_{};
    std::array<float, viz::LfoEngine::kSlots> envDepth_{};
    Frame frame_;
};

}  // namespace geode::signal
