#include "viz/MusicSignals.hpp"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "viz/Program.hpp"

namespace geode::viz {

namespace {

constexpr float kTwoPi = 6.2831853f;

bool fired(float pulse) { return pulse > 0.5f; }

}  // namespace

MusicSignals::MusicSignals()
    : kick_(kDrumRiseHz, kKickFallHz),
      snare_(kDrumRiseHz, kSnareFallHz),
      hat_(kDrumRiseHz, kHatFallHz),
      drop_(kDropRiseHz, kDropFallHz),
      arrival_(kArrivalRiseHz, kArrivalFallHz) {}

void MusicSignals::reset() {
    kick_.reset();
    snare_.reset();
    hat_.reset();
    drop_.reset();
    arrival_.reset();
    build_ = 0.0f;
    novelty_ = 0.0f;
    sectionPhase_ = 0.0f;
    sectionTarget_ = 0.0f;
    barPhase_ = 0.0f;
    beatInBar_ = 0.0f;
    rhythmLock_ = 0.0f;
    harmonic_ = 0.5f;
    brightness_ = 0.0f;
    keyHue_ = 0.0f;
    keyStrength_ = 0.0f;
    pan_ = 0.0f;
    width_ = 0.0f;
    snareFired_ = dropFired_ = arrivalFired_ = sectionFired_ = downbeatFired_ = false;
}

// The circular mean of the twelve pitch-class weights. A plain argmax would
// jump a whole semitone the frame a chord changes; the mean moves with the
// balance of the chord, so a passing note bends the hue rather than swapping
// it.
float MusicSignals::chromaHue(const float* chroma) {
    float x = 0.0f;
    float y = 0.0f;
    float total = 0.0f;
    for (int i = 0; i < GEODE_CHROMA_BINS; ++i) {
        const float w = std::max(chroma[i], 0.0f);
        const float ang = kTwoPi * static_cast<float>(i) / static_cast<float>(GEODE_CHROMA_BINS);
        x += w * std::cos(ang);
        y += w * std::sin(ang);
        total += w;
    }
    if (total <= 1e-6f) return -1.0f;
    float hue = std::atan2(y, x) / kTwoPi;
    if (hue < 0.0f) hue += 1.0f;
    return hue;
}

void MusicSignals::step(const GeodeFeatureFrame& f, float dt) {
    // Drums. Below the floor a hit is ignored entirely rather than fed in
    // small: the envelopes are meant to move geometry, and geometry that
    // twitches on every ghost note reads as noise.
    if (f.kick >= kDrumFloor) kick_.trigger(f.kick);
    snareFired_ = f.snare >= kDrumFloor;
    if (snareFired_) snare_.trigger(f.snare);
    if (f.hat >= kDrumFloor) hat_.trigger(f.hat);
    kick_.step(dt);
    snare_.step(dt);
    hat_.step(dt);

    // Structure.
    build_ = slewTo(build_, std::clamp(f.buildup, 0.0f, 1.0f), dt, kBuildHz, kBuildHz);
    dropFired_ = fired(f.drop);
    arrivalFired_ = fired(f.arrival);
    sectionFired_ = fired(f.sectionBoundary);
    if (dropFired_) drop_.trigger(1.0f);
    if (arrivalFired_) arrival_.trigger(1.0f);
    drop_.step(dt);
    arrival_.step(dt);
    novelty_ = slewTo(novelty_, std::clamp(f.novelty, 0.0f, 1.0f), dt, kNoveltyHz, kNoveltyHz);
    // A drop counts as a section change too: it is the most audible boundary
    // there is, and the structure tracker's own boundary detector has an eight
    // second refractory that can sit across it.
    if (sectionFired_ || dropFired_) sectionTarget_ = std::fmod(sectionTarget_ + kSectionStep, 1.0f);
    sectionPhase_ = std::fmod(
        sectionPhase_ + wrappedDelta(sectionPhase_, sectionTarget_, 1.0f) * (1.0f - std::exp(-dt * kSectionGlideHz)) + 1.0f, 1.0f);

    // Rhythm. The phases are passed through: they are clocks, and smoothing a
    // clock is how a phase-locked motion drifts off the beat. What IS smoothed
    // is how far to trust them.
    barPhase_ = std::clamp(f.barPhase, 0.0f, 1.0f);
    beatInBar_ = f.beatInBar;
    downbeatFired_ = fired(f.downbeat);
    const float lock = std::clamp(f.pulseConfidence, 0.0f, 1.0f) * std::clamp(f.tempoStability, 0.0f, 1.0f);
    rhythmLock_ = slewTo(rhythmLock_, lock, dt, kRhythmLockRiseHz, kRhythmLockFallHz);

    // Tonality.
    harmonic_ = slewTo(harmonic_, std::clamp(f.harmonicity, 0.0f, 1.0f), dt, kHarmonicHz, kHarmonicHz);
    brightness_ = slewTo(brightness_, std::clamp(f.centroid, 0.0f, 1.0f), dt, kBrightnessHz, kBrightnessHz);
    const float confidence = std::clamp(f.chromaConfidence, 0.0f, 1.0f);
    const float hue = confidence >= kKeyConfidenceFloor ? chromaHue(f.chroma) : -1.0f;
    if (hue >= 0.0f) {
        keyHue_ = std::fmod(keyHue_ + wrappedDelta(keyHue_, hue, 1.0f) * (1.0f - std::exp(-dt * kKeyHueHz)) + 1.0f, 1.0f);
    }
    keyStrength_ = slewTo(keyStrength_, hue >= 0.0f ? confidence : 0.0f, dt, kKeyStrengthRiseHz, kKeyStrengthFallHz);

    // Stereo.
    pan_ = slewTo(pan_, std::clamp(f.stereoPan, -1.0f, 1.0f), dt, kStereoHz, kStereoHz);
    width_ = slewTo(width_, std::clamp(f.stereoWidth, 0.0f, 1.0f), dt, kStereoHz, kStereoHz);
}

// Every style is handed these; a style that reads none of them links them away
// and glUniform on a -1 location is a no-op, exactly as ShaderScene::uploadMotion.
void MusicSignals::upload(UniformCache& u) const {
    glUniform1f(u.loc("uKick"), kick());
    glUniform1f(u.loc("uSnare"), snare());
    glUniform1f(u.loc("uHat"), hat());
    glUniform1f(u.loc("uBuild"), build_);
    glUniform1f(u.loc("uDrop"), drop());
    glUniform1f(u.loc("uArrival"), arrival());
    glUniform1f(u.loc("uNovelty"), novelty_);
    glUniform1f(u.loc("uSectionPhase"), sectionPhase_);
    glUniform1f(u.loc("uBarPhase"), barPhase_);
    glUniform1f(u.loc("uBeatInBar"), beatInBar_);
    glUniform1f(u.loc("uRhythmLock"), rhythmLock_);
    glUniform1f(u.loc("uHarmonic"), harmonic_);
    glUniform1f(u.loc("uBrightSmooth"), brightness_);
    glUniform1f(u.loc("uKeyHue"), keyHue_);
    glUniform1f(u.loc("uKeyStrength"), keyStrength_);
    glUniform1f(u.loc("uPanSmooth"), pan_);
    glUniform1f(u.loc("uWidthSmooth"), width_);
}

}  // namespace geode::viz
