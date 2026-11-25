#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "viz/resolver/EngineSettings.hpp"
#include "sound/SoundFrame.h"
#include "viz/modulation/Adsr.hpp"
#include "viz/CameraRig.hpp"
#include "viz/director/Director.hpp"
#include "viz/director/DirectorReach.hpp"
#include "viz/modulation/Lfo.hpp"
#include "viz/modulation/MacroBank.hpp"
#include "viz/params/Params.hpp"
#include "viz/modulation/Reactions.hpp"
#include "viz/modulation/Responder.hpp"

namespace geode::signal {

// The scene params resolver (GLOSSARY): the engine's per-frame chain that turns the params the shell asked
// for into the ones a scene is drawn with. One step a frame, in this order, and nowhere else:
//
//   param fade (the style's own paramFadeSec, or a preset morph)
//   → the director's shuffle (rolled offsets over the user's params, when its reach asks)
//   → auto-director (its look lies under everything after it)
//   → the director's axes (its behaviour offsets; never a colour) → the director's own modulators (its reach)
//   → the macro bank (the user's knobs, an offset added to whatever the director left)
//   → LFOs → envelopes
//   → reaction routes (the user's or the style's DNA; the director plays no route)
//   → Spin (a steady turn on a style that turns)
//   → safety::apply, last, so nothing before it can pass the flash and motion limits
//
// WHY a module of its own (#344): the chain used to sit inside the GL renderer, behind its mutex and
// atomics, reading the process-wide knobs and hand mask live. The order was only
// visible on a device, and an export heard live edits. Here it knows nothing of GL or threads: everything
// it reads arrives as a plain value (EngineSettings, StyleContext, the sound frame, dt), so the order is
// host-tested in one place (core/tests/resolver_test.cpp), two resolvers given the same inputs give the
// same bits, and every renderer (live, wallpaper, dream, export) runs the same chain.
//
// Single-threaded: no mutex, no atomics. The renderer keeps its lock and atomics and hands the resolver
// copies at its frame latch. Device policy (post-FX), the camera rig
// and the post pass's integrators stay in the renderer, after the step.
//
// It depends on core/sound (the frame) and on the GL-free stage code still in core/viz (Director,
// Lfo, Adsr, Reactions, MacroBank, VisualSafety, Catalogue); never on a GL header.
// Later steps (route producers, base plus modulation, one clamp) rebuild behind this interface.

// The chain reads one EngineSettings value a frame (viz/resolver/EngineSettings.hpp): the renderer's SettingsLatch
// hands it the process globals live, or an export's copy pinned at prepare (#350).

// The style on screen this frame.
struct StyleContext {
    const std::string* id = nullptr;  // null when no style is on screen
    float spin = 0.0f;                // SceneRegistry::spinRate of the style: 0 holds still
};

// What a style that turns (spinRate 1, the Spin dial at 1, Speed 1) adds to Rotation, in radians a second: a steady
// turn, never set by the sound.
inline constexpr float kSpinRadPerSecond = 0.5f;

class Resolver {
public:
    struct Frame {
        // The params a scene is drawn with, before the renderer's device masks.
        viz::SceneParams params;
        // The director's style moments this step: a look committed at a new section or a drop (Auto
        // changes style on each).
        int styleMoments = 0;
        // The camera rig's music moves: the director's Space and Chaos axes under its look.
        viz::CameraRig::Moves cameraMoves{};
        // What the waves and envelopes on a macro knob added to its turn this step (the knob's second arc).
        viz::macrobank::KnobMods knobMods{};
    };

    // Per-renderer configuration, kept until changed. Plain copies; call from the stepping thread.
    void setLfoConfigs(const std::array<viz::LfoConfig, viz::LfoEngine::kSlots>& configs) { lfo_.configs = configs; }
    void setAdsrConfigs(const std::array<viz::AdsrConfig, viz::AdsrEngine::kCount>& configs) { adsr_.configs = configs; }
    void setReactionRoutes(const std::array<viz::ReactRoute, viz::ReactionEngine::kRoutes>& routes) {
        handRoutes_ = routes;
        recompile();
    }
    // The Event Responder's rows and responses (viz/modulation/Responder): they compile into the matrix's free slots
    // beside the routes above, and play from the step after.
    void setResponses(const viz::responder::Config& config) {
        responder_.configure(config);
        recompile();
    }
    // Plays one response once, on the next step (the picker's preview).
    void previewResponse(const viz::responder::Response& response) {
        responder_.preview(response);
        recompile();
    }
    const viz::responder::Bank& responder() const { return responder_; }
    // The song starts over (a new play: track change, seek, an export's start): the responses' counters, cooldowns,
    // envelopes and seeded chance start again, so the same song from here gives the same fires.
    void restartPlay() { responder_.reset(); }
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

    const viz::Director& director() const { return director_; }
    const viz::DirectorReach& directorReach() const { return directorReach_; }

private:
    // What the slots on LfoTarget::Macro add to their knobs this step.
    viz::macrobank::KnobMods knobModsOf(const std::array<float, viz::LfoEngine::kSlots>& waves,
                                        const std::array<float, viz::AdsrEngine::kCount>& envs) const;

    // The matrix's routes as the style and the user deal them, and the matrix with the responses compiled in.
    void recompile() {
        reactions_.routes = handRoutes_;
        responder_.compile(reactions_.routes);
    }

    viz::LfoEngine lfo_;
    viz::AdsrEngine adsr_;
    viz::ReactionEngine reactions_;
    std::array<viz::ReactRoute, viz::ReactionEngine::kRoutes> handRoutes_{};
    viz::responder::Bank responder_;
    // Customize › Music's moments as they come, for the waves and envelopes set "On a moment".
    viz::MomentWatch momentWatch_;
    viz::Director director_;
    int directorCommitsSeen_ = 0;
    viz::DirectorReach directorReach_;

    // The faded params, before the director; what the next frame fades from.
    viz::SceneParams displayed_;
    float morphFadeSec_ = 0.0f;
    float morphRemainSec_ = 0.0f;
    // Which targets the macro bank may move on the style on screen, worked out when the style changes.
    std::string reachStyle_;
    viz::macrobank::Reach reach_;
    bool reachKnown_ = false;
    std::array<float, viz::LfoEngine::kSlots> envRate_{};
    std::array<float, viz::LfoEngine::kSlots> envDepth_{};
    Frame frame_;
};

}  // namespace geode::signal
