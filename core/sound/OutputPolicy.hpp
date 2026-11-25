#pragma once

#include <cstddef>
#include <cstdint>

#include "sound/SoundFrame.h"

// One row per GeodeSoundFrame output: how it is ranged, how it is smoothed and how it decays. The code that does
// the ranging and smoothing reads its constants from the named parameter sets below, and the table says which output
// uses which, so a constant has one home. Rows follow the frame's declaration order, as interp/FrameBlend's
// kFieldKinds does (its asserts keep the two in step, so a field is added to both).
namespace geode::sound::policy {

enum class Norm : uint8_t {
    Internal,        // ranged inside its own stage (a detector's state, a fixed scale in the stage's code)
    RelativeToMean,  // energy over its own running mean (RelativeLevel): 1.0 is typical for this song right now
    Fixed,           // a fixed range
    Told,            // not heard: what the player says
    Drawn,           // drawn per frame from the ring, not a number the engine computes
};

enum class Smoothing : uint8_t {
    None,      // the value is as the stage made it
    Internal,  // a detector's own state machine (hysteresis, refractory, hold): not an attack and a release
    Pole,      // a one-pole per frame at 30 fps (perFrame): attack and release are factors, not seconds
    Envelope,  // a trigger's envelope: rises over `attack` seconds, decays over `decay` seconds
    Lag,       // follow() with `attack` seconds up and `release` seconds down
    Ease,      // follow() with one time constant (attack = release)
    Rate,      // a leaky count: decays with time constant `decay` seconds
    Tuned,     // the user's attack and release (Settings); `attack` and `release` are their defaults in seconds
};

struct Params {
    Norm norm = Norm::Internal;
    Smoothing smoothing = Smoothing::Internal;
    float attack = 0.0f;
    float release = 0.0f;
    float decay = 0.0f;       // 0 when it has none of its own (the drum envelopes take the user's drumDecay)
    float meanWarm = 0.0f;    // RelativeToMean: the running mean's pole while the engine settles
    float meanSteady = 0.0f;  // and once it has (~4.15 s)
};

// Bands, flux, hfc and the Band EQ: energy over its own running mean, with a fast envelope over the same mean.
inline constexpr Params kBandLevel{Norm::RelativeToMean, Smoothing::Pole, 0.2f, 0.5f, 0.0f, 0.9f, 0.992f};
// low / middle / high: the relative bands through the user's attack, release, threshold, gain and offset.
inline constexpr Params kShaped{Norm::Fixed, Smoothing::Tuned, 0.03f, 0.15f, 0.0f, 0.0f, 0.0f};
// kick / snare / hat envelopes: 5 ms up, then the user's drum decay.
inline constexpr Params kDrumEnvelope{Norm::Fixed, Smoothing::Envelope, 0.005f, 0.0f, 0.0f, 0.0f, 0.0f};
// onsetEnv and transientEnv: 5 ms up, 0.2 s down.
inline constexpr Params kOnsetEnvelope{Norm::Fixed, Smoothing::Envelope, 0.005f, 0.0f, 0.2f, 0.0f, 0.0f};
// Toggles and randoms eased between their steps.
inline constexpr Params kEased{Norm::Fixed, Smoothing::Ease, 0.06f, 0.06f, 0.0f, 0.0f, 0.0f};
// drive: 0.5 s up, 1.5 s down.
inline constexpr Params kDrive{Norm::Fixed, Smoothing::Lag, 0.5f, 1.5f, 0.0f, 0.0f, 0.0f};
// runDensity: note changes over a 1 s time constant.
inline constexpr Params kRunRate{Norm::Fixed, Smoothing::Rate, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
// busy: onsets and hits counted over a 2 s time constant.
inline constexpr Params kBusyRate{Norm::Fixed, Smoothing::Rate, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
// One-hop pulses: their detectors keep their own state; nothing is smoothed.
inline constexpr Params kTrigger{Norm::Internal, Smoothing::Internal, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
inline constexpr Params kTold{Norm::Told, Smoothing::None, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
inline constexpr Params kDrawn{Norm::Drawn, Smoothing::None, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
// Everything else: ranged and smoothed inside its own stage (Rhythm, Harmony, Form, the pitch estimator ...).
inline constexpr Params kInternal{};

struct Row {
    const char* name;
    size_t offset;  // bytes into GeodeSoundFrame
    int count;      // floats
    Params params;
};

#define GEODE_OUTPUT(name, params) \
    Row { #name, offsetof(GeodeSoundFrame, name), static_cast<int>(sizeof(GeodeSoundFrame::name) / sizeof(float)), params }

inline constexpr Row kOutputs[] = {
    GEODE_OUTPUT(bass, kBandLevel), GEODE_OUTPUT(mid, kBandLevel), GEODE_OUTPUT(treble, kBandLevel),
    GEODE_OUTPUT(vol, kInternal), GEODE_OUTPUT(bassAtt, kBandLevel), GEODE_OUTPUT(midAtt, kBandLevel),
    GEODE_OUTPUT(trebleAtt, kBandLevel), GEODE_OUTPUT(volAtt, kInternal), GEODE_OUTPUT(sub, kBandLevel),
    GEODE_OUTPUT(subAtt, kBandLevel), GEODE_OUTPUT(midHigh, kBandLevel), GEODE_OUTPUT(midHighAtt, kBandLevel),
    GEODE_OUTPUT(low, kShaped), GEODE_OUTPUT(middle, kShaped), GEODE_OUTPUT(high, kShaped),
    GEODE_OUTPUT(level, kInternal), GEODE_OUTPUT(bassPresence, kInternal), GEODE_OUTPUT(midPresence, kInternal),
    GEODE_OUTPUT(treblePresence, kInternal), GEODE_OUTPUT(intensity, kInternal),
    GEODE_OUTPUT(bassTime, kInternal), GEODE_OUTPUT(midTime, kInternal), GEODE_OUTPUT(trebleTime, kInternal),
    GEODE_OUTPUT(curvedTime, kInternal), GEODE_OUTPUT(hitTime, kInternal), GEODE_OUTPUT(noteTime, kInternal),
    GEODE_OUTPUT(presence, kInternal), GEODE_OUTPUT(rms, kInternal), GEODE_OUTPUT(loudness, kInternal),
    GEODE_OUTPUT(centroid, kInternal), GEODE_OUTPUT(rolloff, kInternal), GEODE_OUTPUT(flatness, kInternal),
    GEODE_OUTPUT(densitySlow, kInternal), GEODE_OUTPUT(densityFast, kInternal), GEODE_OUTPUT(flux, kBandLevel),
    GEODE_OUTPUT(energyTrend, kInternal), GEODE_OUTPUT(peakFreq, kInternal), GEODE_OUTPUT(peakMag, kInternal),
    GEODE_OUTPUT(stereoWidth, kInternal), GEODE_OUTPUT(stereoPan, kInternal),
    GEODE_OUTPUT(kickEnv, kDrumEnvelope), GEODE_OUTPUT(snareEnv, kDrumEnvelope),
    GEODE_OUTPUT(hatEnv, kDrumEnvelope), GEODE_OUTPUT(onsetEnv, kOnsetEnvelope),
    GEODE_OUTPUT(kickToggle, kEased), GEODE_OUTPUT(snareToggle, kEased), GEODE_OUTPUT(kickRandom, kEased),
    GEODE_OUTPUT(noteRandom, kEased), GEODE_OUTPUT(kick, kTrigger), GEODE_OUTPUT(snare, kTrigger),
    GEODE_OUTPUT(hat, kTrigger), GEODE_OUTPUT(kick2, kTrigger), GEODE_OUTPUT(kick4, kTrigger),
    GEODE_OUTPUT(kick8, kTrigger), GEODE_OUTPUT(kick16, kTrigger), GEODE_OUTPUT(snare2, kTrigger),
    GEODE_OUTPUT(snare4, kTrigger), GEODE_OUTPUT(hardCut, kTrigger), GEODE_OUTPUT(onset, kTrigger),
    GEODE_OUTPUT(silence, kTrigger), GEODE_OUTPUT(returned, kTrigger), GEODE_OUTPUT(surge, kTrigger),
    GEODE_OUTPUT(noteChange, kTrigger), GEODE_OUTPUT(legato, kTrigger), GEODE_OUTPUT(reattack, kTrigger),
    GEODE_OUTPUT(pitch, kInternal), GEODE_OUTPUT(pitchClass, kInternal), GEODE_OUTPUT(pitchHeight, kInternal),
    GEODE_OUTPUT(pitchConfidence, kInternal), GEODE_OUTPUT(interval, kInternal), GEODE_OUTPUT(glide, kInternal),
    GEODE_OUTPUT(vibrato, kInternal), GEODE_OUTPUT(evenness, kInternal), GEODE_OUTPUT(runDensity, kRunRate),
    GEODE_OUTPUT(bpm, kInternal), GEODE_OUTPUT(beatPhase, kInternal), GEODE_OUTPUT(beatConf, kInternal),
    GEODE_OUTPUT(beat, kTrigger), GEODE_OUTPUT(beatCount, kInternal), GEODE_OUTPUT(downbeat, kTrigger),
    GEODE_OUTPUT(barPhase, kInternal), GEODE_OUTPUT(barCount, kInternal), GEODE_OUTPUT(phrase, kTrigger),
    GEODE_OUTPUT(phraseIndex, kInternal), GEODE_OUTPUT(phrasePhase, kInternal),
    GEODE_OUTPUT(fluxLow, kBandLevel), GEODE_OUTPUT(fluxMid, kBandLevel), GEODE_OUTPUT(fluxHigh, kBandLevel),
    GEODE_OUTPUT(hfc, kBandLevel), GEODE_OUTPUT(keyHue, kInternal), GEODE_OUTPUT(keyRoot, kInternal),
    GEODE_OUTPUT(keyMajor, kInternal), GEODE_OUTPUT(keyClarity, kInternal), GEODE_OUTPUT(chordHue, kInternal),
    GEODE_OUTPUT(tension, kInternal), GEODE_OUTPUT(harmonicChange, kTrigger), GEODE_OUTPUT(scale, kInternal),
    GEODE_OUTPUT(scaleRoot, kInternal), GEODE_OUTPUT(scaleBrightness, kInternal),
    GEODE_OUTPUT(scaleTension, kInternal), GEODE_OUTPUT(scaleFit, kInternal), GEODE_OUTPUT(novelty, kInternal),
    GEODE_OUTPUT(sectionChange, kTrigger), GEODE_OUTPUT(sectionId, kInternal),
    GEODE_OUTPUT(sectionState, kInternal), GEODE_OUTPUT(sectionBars, kInternal),
    GEODE_OUTPUT(buildUp, kInternal), GEODE_OUTPUT(drop, kTrigger), GEODE_OUTPUT(fadeGain, kTold),
    GEODE_OUTPUT(loudnessM, kInternal), GEODE_OUTPUT(loudnessS, kInternal), GEODE_OUTPUT(busy, kBusyRate),
    GEODE_OUTPUT(drive, kDrive), GEODE_OUTPUT(transient, kTrigger), GEODE_OUTPUT(transientEnv, kOnsetEnvelope),
    GEODE_OUTPUT(transientSharpness, kInternal), GEODE_OUTPUT(eq, kBandLevel), GEODE_OUTPUT(spectrum, kInternal),
    GEODE_OUTPUT(waveform, kDrawn), GEODE_OUTPUT(chroma, kInternal), GEODE_OUTPUT(chromaFast, kInternal),
    GEODE_OUTPUT(chromaSlow, kInternal),
};

#undef GEODE_OUTPUT

inline constexpr int kOutputRows = static_cast<int>(sizeof(kOutputs) / sizeof(kOutputs[0]));

// The row of the output whose float is at `index` in GeodeSoundFrame (null past the frame).
constexpr const Row* rowOf(int index) {
    int first = 0;
    for (const Row& r : kOutputs) {
        if (index < first + r.count) return &r;
        first += r.count;
    }
    return nullptr;
}

}  // namespace geode::sound::policy
