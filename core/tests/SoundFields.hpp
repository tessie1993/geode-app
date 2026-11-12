#pragma once
// Every scalar of GeodeSoundFrame by name, for the host tests that print or compare whole frames
// (sound_rate_test, sound_golden_test).
#include <cstddef>
#include <cstring>

#include "sound/SoundFrame.h"

namespace geode::sound::test {

// Every scalar of GeodeSoundFrame, in declaration order; the arrays (eq, spectrum, waveform, chroma) follow them.
#define GEODE_SOUND_SCALAR_FIELDS(X)                                                                         \
    X(bass) X(mid) X(treble) X(vol) X(bassAtt) X(midAtt) X(trebleAtt) X(volAtt) X(sub) X(subAtt) X(midHigh) \
    X(midHighAtt) X(low) X(middle) X(high) X(level) X(bassPresence) X(midPresence) X(treblePresence)        \
    X(intensity) X(bassTime) X(midTime) X(trebleTime) X(curvedTime) X(hitTime) X(noteTime) X(presence)      \
    X(rms) X(loudness) X(centroid) X(rolloff) X(flatness) X(densitySlow) X(densityFast) X(flux)             \
    X(energyTrend) X(peakFreq) X(peakMag) X(stereoWidth) X(stereoPan) X(kickEnv) X(snareEnv) X(hatEnv)      \
    X(onsetEnv) X(kickToggle) X(snareToggle) X(kickRandom) X(noteRandom) X(kick) X(snare) X(hat) X(kick2)   \
    X(kick4) X(kick8) X(kick16) X(snare2) X(snare4) X(hardCut) X(onset) X(silence) X(returned) X(surge)     \
    X(noteChange) X(legato) X(reattack) X(pitch) X(pitchClass) X(pitchHeight) X(pitchConfidence)            \
    X(interval) X(glide) X(vibrato) X(evenness) X(runDensity) X(bpm) X(beatPhase) X(beatConf) X(beat)       \
    X(beatCount) X(downbeat) X(barPhase) X(barCount) X(phrase) X(phraseIndex) X(phrasePhase) X(fluxLow)     \
    X(fluxMid) X(fluxHigh) X(hfc) X(keyHue) X(keyRoot) X(keyMajor) X(keyClarity) X(chordHue) X(tension)     \
    X(harmonicChange) X(scale) X(scaleRoot) X(scaleBrightness) X(scaleTension) X(scaleFit) X(novelty)       \
    X(sectionChange) X(sectionId) X(sectionState) X(sectionBars) X(buildUp) X(drop) X(fadeGain)            \
    X(loudnessM) X(loudnessS) X(busy) X(drive) X(transient) X(transientEnv) X(transientSharpness)

struct Field {
    const char* name;
    float GeodeSoundFrame::* member;
};
#define GEODE_FIELD_ENTRY(n) Field{#n, &GeodeSoundFrame::n},
constexpr Field kFields[] = {GEODE_SOUND_SCALAR_FIELDS(GEODE_FIELD_ENTRY)};
#undef GEODE_FIELD_ENTRY
constexpr int kFieldCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));
static_assert(kFieldCount == GEODE_SOUND_SCALARS, "a GeodeSoundFrame scalar was added or removed: update the list");
static_assert(offsetof(GeodeSoundFrame, eq) == GEODE_SOUND_SCALARS * sizeof(float), "scalars come first");

inline int fieldIndex(const char* name) {
    for (int k = 0; k < kFieldCount; k++) {
        if (std::strcmp(kFields[k].name, name) == 0) return k;
    }
    return -1;
}

}  // namespace geode::sound::test
