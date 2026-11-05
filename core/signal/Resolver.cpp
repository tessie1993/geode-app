#include "signal/Resolver.hpp"

#include <algorithm>

#include "viz/VisualSafety.hpp"

namespace geode::signal {

namespace {
// The id the motion field's variant is seeded from when no style is on screen.
const std::string kNoStyle;
}  // namespace

void Resolver::beginMorph(float seconds) {
    if (seconds <= 0.0f) return;
    morphFadeSec_ = seconds;
    morphRemainSec_ = seconds * 3.0f;
}

void Resolver::reset() {
    // Exactly what a new surface has always reset; the LFO and envelope phases, the fade and the director's
    // commit count carry on (a reset director counts from 0, so the next commit is a new moment again).
    motionField_.reset();
    reactions_.reset();
    macroReactions_.reset();
    director_.reset();
    notes_.reset();
}

const Resolver::Frame& Resolver::step(const viz::SceneParams& requested, const GeodeSoundFrame& sound, float dt,
                                      const EngineSettings& settings, const StyleContext& style) {
    using viz::SceneParams;
    const std::string& styleId = style.id != nullptr ? *style.id : kNoStyle;
    if (style.switched) motionField_.setVariant(viz::styleVariantSeed(styleId));

    float morph;
    if (morphRemainSec_ > 0.0f) {
        morphRemainSec_ -= dt;
        morph = morphFadeSec_;
    } else {
        morph = 0.0f;
    }
    const float fade = std::max(requested.paramFadeSec, morph);
    displayed_ = fade <= 0.01f ? requested : viz::lerpParams(displayed_, requested, std::clamp(dt / fade, 0.0f, 1.0f));

    // The auto-director hears the frame first: its look lies under the macros, so the user's own knobs,
    // the LFOs, the reactions, the motion field and the safety clamp all still act on top of it.
    director_.configure(settings.director);
    director_.step(sound, dt, settings.handMask);
    frame_.styleMoments = 0;
    if (director_.commits() != directorCommitsSeen_) {
        directorCommitsSeen_ = director_.commits();
        const viz::Director::Reason why = director_.lastReason();
        if (why == viz::Director::Reason::Section || why == viz::Director::Reason::Drop) frame_.styleMoments = 1;
    }
    directedKnobs_ = director_.knobs(settings.knobs);
    // Note & key reactions hear the frame next, inside the director's look: its hue anchors the notes'
    // colours, and its feeling (the director's emotion, stepped whether or not it drives) is the mood. Its
    // tuning is the style's own (Customize), read before the director and macros touch it.
    notes_.configure({settings.notesOn, settings.director.seed ^ settings.noteEntropy});
    {
        const SceneParams& own = displayed_;
        viz::NoteResponse::Inputs in;
        in.anchorHue = director_.look().hue;
        in.anchorWeight = director_.weight();
        in.color = own.noteColor;
        in.pitch = own.notePitch;
        in.intervals = own.noteIntervals;
        in.tension = own.noteTension;
        in.chords = own.noteChords;
        in.drums = own.noteDrums;
        notes_.step(sound, director_.emotion(), in, dt);
    }
    const auto& envValues = adsr_.tick(dt, sound);
    viz::AdsrEngine::lfoOffsets(adsr_.configs, envValues, envRate_, envDepth_);
    const auto& lfoValues = lfo_.tick(dt, sound, envRate_.data(), envDepth_.data());
    // Order: the director's look, the note layer inside it, then the user's macros and LFOs over both. The
    // macros lay over the user's params first, so every stage after them (and the safety clamp) sees the
    // knobbed look.
    SceneParams p = lfo_.apply(viz::macros::apply(notes_.apply(director_.apply(displayed_, style.shaderFamily)), directedKnobs_), lfoValues);
    p = viz::AdsrEngine::apply(p, adsr_.configs, envValues);
    // What the envelopes and LFOs add to the luminance targets this frame: the flash limiter counts it
    // with the measured output, so an envelope strobe engages the hold at once (#363).
    frame_.luminanceDrive.fill(0.0f);
    lfo_.addLuminanceDrive(lfoValues, frame_.luminanceDrive);
    viz::AdsrEngine::addLuminanceDrive(adsr_.configs, envValues, frame_.luminanceDrive);
    // The reaction matrix: per-style routes from the sound frame to many targets. Ahead of the motion
    // field and the safety clamp, like the LFOs.
    reactions_.tick(sound, dt);
    p = reactions_.apply(p);
    // The macros' own routes, in an engine of their own so they never take a user slot.
    macroReactions_.routes = viz::macros::routes(directedKnobs_);
    macroReactions_.tick(sound, dt);
    p = macroReactions_.apply(p);
    // The director's routes, crossfading at each decision, in engines of their own.
    p = director_.applyRoutes(p);
    // The continuous motion system: the one stage every family's parameters pass through, fed the sound
    // frame (never a single drum hit or PCM impulse), ahead of the safety clamp so nothing it adds can
    // exceed the flash and motion limits.
    motionField_.step(sound, dt);
    p = motionField_.apply(p, settings.reducedMotion, style.spin);
    p = viz::safety::apply(p, settings.reducedMotion);
    frame_.params = p;
    frame_.cameraMoves = director_.moves(viz::macros::camera(directedKnobs_));
    return frame_;
}

}  // namespace geode::signal
