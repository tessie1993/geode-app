#pragma once
#include <vector>

#include "sound/Emotion.hpp"
#include "sound/offline/OfflineTrack.hpp"

namespace geode::sound::offline {

// What the music feels like, for the whole track at once (#464, ADR 0011): live Emotion's model (the key
// and scale's fingerprint from EmotionTable.inc, three quarters the mode's measured emotion and a quarter
// the key's character; loudness, tempo, onset rate, brightness, harmony tension and build-up as live cues;
// the leading scale degree's pull) read from the offline analysis instead of the live frame, with every
// follower replaced by a centred mean of the same reach: the fingerprint and its confidence ±3 s, valence
// ±3 s, energy ±1.5 s, tension ±1 s, GEMS ±2 s, the degree's pull ±0.3 s. A new key or scale needs no
// one-second hold: the Viterbi path in OfflineHarmony already decided where the music changes. A leading
// degree must still last 0.12 s (shorter ones join the one before), and degreeAccent fires where it starts.
//
// It fills Emotion::State, the type the director reads, so an export's director can take its feeling from
// here exactly as the screen's takes it from Emotion::step.
// The build-up cue is OfflineForm's buildUp (#465), read through sampleTrack like every other cue.
struct EmotionTrack {
    double frameRate = 0.0;
    std::vector<Emotion::State> frames;  // degreeAccent is 0 here; it fires from `accents`
    std::vector<double> accents;         // seconds where a new leading degree starts
};

EmotionTrack analyseEmotion(const Track& track);

// The feeling for the video frame that ends at `seconds` and lasted `dt`.
Emotion::State sampleEmotion(const EmotionTrack& emotion, double seconds, double dt);

}  // namespace geode::sound::offline
