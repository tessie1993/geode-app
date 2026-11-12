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

viz::macrobank::KnobMods Resolver::knobModsOf(const std::array<float, viz::LfoEngine::kSlots>& waves,
                                              const std::array<float, viz::AdsrEngine::kCount>& envs) const {
    using viz::LfoTarget;
    using viz::macrobank::KnobMods;
    KnobMods mods;
    for (size_t i = 0; i < waves.size(); ++i) {
        const viz::LfoConfig& c = lfo_.configs[i];
        if (!c.enabled || c.target != LfoTarget::Macro || c.knob <= 0) continue;
        // How far the wave can turn the knob: its depth, one way or both (its polarity).
        const float depth = std::clamp(c.depth, 0.0f, 1.0f);
        const float lo = c.polarity == viz::ModPolarity::Positive ? 0.0f : -depth;
        const float hi = c.polarity == viz::ModPolarity::Negative ? 0.0f : depth;
        mods.add(c.knob, waves[i], KnobMods::kWave, lo, hi);
    }
    for (size_t i = 0; i < envs.size(); ++i) {
        const viz::AdsrConfig& c = adsr_.configs[i];
        if (!c.enabled || c.knob <= 0 || std::find(c.targets.begin(), c.targets.end(), LfoTarget::Macro) == c.targets.end()) continue;
        mods.add(c.knob, envs[i] * c.amount, KnobMods::kEnvelope, 0.0f, c.amount);
    }
    return mods;
}

void Resolver::reset() {
    // Exactly what a new surface has always reset; the LFO and envelope phases, the fade and the director's
    // commit count carry on (a reset director counts from 0, so the next commit is a new moment again).
    motionField_.reset();
    reactions_.reset();
    director_.reset();
    directorReach_.reset();
    notes_.reset();
    momentWatch_.reset();
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
        director_.step(sound, *heard, dt);
    } else {
        director_.step(sound, dt);
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
    // Note & key reactions hear the frame next, with the feeling the director heard (stepped whether or
    // not it drives). They publish the style knobs only and write no param. Their tuning is the style's
    // own (Customize).
    notes_.configure({settings.notesOn, settings.director.seed ^ settings.noteEntropy});
    {
        const SceneParams& own = displayed_;
        viz::NoteResponse::Inputs in;
        in.pitch = own.notePitch;
        in.intervals = own.noteIntervals;
        in.tension = own.noteTension;
        in.chords = own.noteChords;
        in.drums = own.noteDrums;
        notes_.step(sound, director_.emotion(), in, dt);
    }
    // The reaction matrix hears the frame before the waves and envelopes, so a slot set "On a moment" restarts or
    // fires on this frame's moment (MomentWatch, the matrix's Step rule); its routes still land after them.
    reactions_.tick(sound, dt);
    const std::uint32_t moments = momentWatch_.step(reactions_.signals());
    const auto& envValues = adsr_.tick(dt, sound, moments);
    viz::AdsrEngine::lfoOffsets(adsr_.configs, envValues, envRate_, envDepth_);
    const auto& lfoValues = lfo_.tick(dt, sound, envRate_.data(), envDepth_.data(), moments);
    // A wave or an envelope on a macro knob turns the knob, not a param: it is added to the knob's turn where the
    // bank runs, so the knob's links move with it, and the user's turn is never rewritten (base plus modulation).
    frame_.knobMods = knobModsOf(lfoValues, envValues);
    // Order: the director's shuffle, its look, its axes and modulators, then the user's macro bank and LFOs
    // over all of it. The bank adds its offsets to what the director left (base plus modulation), so every
    // stage after it (and the safety clamp) sees the knobbed look.
    if (!reachKnown_ || styleId != reachStyle_) {
        reach_ = viz::macrobank::reachMask(styleId);
        reachStyle_ = styleId;
        reachKnown_ = true;
    }
    SceneParams p = director_.applyAxes(director_.apply(directorReach_.applyShuffle(displayed_)));
    p = directorReach_.applyModulation(p);
    if (settings.bank) p = viz::macrobank::apply(p, *settings.bank, reach_, &frame_.knobMods);
    p = lfo_.apply(p, lfoValues);
    p = viz::AdsrEngine::apply(p, adsr_.configs, envValues);
    // The reaction matrix: per-style routes from the sound frame to many targets. Ahead of the motion
    // field and the safety clamp, like the LFOs.
    p = reactions_.apply(p);
    // No stage follows the matrix with routes of its own: the director plays none (owner's rule).
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
