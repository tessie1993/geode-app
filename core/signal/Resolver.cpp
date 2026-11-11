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
    director_.reset();
    directorReach_.reset();
    notes_.reset();
}

const Resolver::Frame& Resolver::step(const viz::SceneParams& requested, const GeodeSoundFrame& sound, float dt,
                                      const EngineSettings& settings, const StyleContext& style,
                                      const sound::Emotion::State* heard, float fadeDt) {
    using viz::SceneParams;
    const std::string& styleId = style.id != nullptr ? *style.id : kNoStyle;
    if (style.switched) motionField_.setVariant(viz::styleVariantSeed(styleId));

    // The glide to what the user asked for runs on the caller's own step when it gives one: the renderer's
    // dt stops in silence, and a slider moved then must still arrive.
    const float glide = fadeDt >= 0.0f ? fadeDt : dt;
    float morph;
    if (morphRemainSec_ > 0.0f) {
        morphRemainSec_ -= glide;
        morph = morphFadeSec_;
    } else {
        morph = 0.0f;
    }
    const float fade = std::max(requested.paramFadeSec, morph);
    displayed_ = fade <= 0.01f ? requested : viz::lerpParams(displayed_, requested, std::clamp(glide / fade, 0.0f, 1.0f));

    // The auto-director hears the frame first: its look lies under the user's knobs, so the macro bank,
    // the LFOs, the reactions, the motion field and the safety clamp all still act on top of it.
    director_.configure(settings.director);
    // The engine's own feeling when the caller has one (the renderer), else the director steps its own.
    if (heard != nullptr) {
        director_.step(sound, *heard, dt, settings.handMask);
    } else {
        director_.step(sound, dt, settings.handMask);
    }
    frame_.styleMoments = 0;
    const bool committed = director_.commits() != directorCommitsSeen_;
    if (committed) {
        directorCommitsSeen_ = director_.commits();
        if (director_.styleMoment()) frame_.styleMoments = 1;
    }
    {
        viz::DirectorReach::Inputs in;
        in.reach = settings.reach;
        in.seed = settings.director.seed;
        in.handMask = settings.handMask;
        in.reducedMotion = settings.reducedMotion;
        in.kept = settings.kept;
        in.styleId = &styleId;
        in.weight = director_.weight();
        in.committed = committed;
        in.reason = director_.lastReason();
        in.commit = director_.commits();
        in.energy = director_.target().energy;
        in.wallDt = glide;
        directorReach_.step(requested, sound, dt, in);
    }
    // Note & key reactions hear the frame next, inside the director's look: its hue anchors the notes'
    // colours, and its feeling (the director's emotion, stepped whether or not it drives) is the mood. Its
    // tuning is the style's own (Customize), read before the director and the knobs touch it.
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
    // Order: the director's shuffle, its look, the note layer inside it, its axes and modulators, then the user's macro bank
    // and LFOs over all of it. The bank adds its offsets to what the director left (base plus modulation),
    // so every stage after it (and the safety clamp) sees the knobbed look.
    if (!reachKnown_ || styleId != reachStyle_) {
        reach_ = viz::macrobank::reachMask(styleId);
        reachStyle_ = styleId;
        reachKnown_ = true;
    }
    SceneParams p = director_.applyAxes(notes_.apply(director_.apply(directorReach_.applyShuffle(displayed_), style.shaderFamily)));
    p = directorReach_.applyModulation(p);
    if (settings.bank) p = viz::macrobank::apply(p, *settings.bank, reach_);
    p = lfo_.apply(p, lfoValues);
    p = viz::AdsrEngine::apply(p, adsr_.configs, envValues);
    // The reaction matrix: per-style routes from the sound frame to many targets. Ahead of the motion
    // field and the safety clamp, like the LFOs.
    reactions_.tick(sound, dt);
    p = reactions_.apply(p);
    // The director's routes: its axes', then its look's, crossfading at each decision, in engines of their own.
    p = director_.applyRoutes(p);
    // The continuous motion system: the one stage every family's parameters pass through, fed the sound
    // frame (never a single drum hit or PCM impulse), ahead of the safety clamp so nothing it adds can
    // exceed the flash and motion limits.
    motionField_.step(sound, dt);
    p = motionField_.apply(p, settings.reducedMotion, style.spin);
    p = viz::safety::apply(p, settings.reducedMotion);
    frame_.params = p;
    frame_.cameraMoves = director_.moves();
    return frame_;
}

}  // namespace geode::signal
