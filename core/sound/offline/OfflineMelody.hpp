#pragma once
#include <cstddef>
#include <vector>

#include "sound/SoundFrame.h"
#include "sound/offline/OfflineFeatures.hpp"

namespace geode::sound::offline {

// The melody fields (#464, ADR 0011), offline. Two steps, so a change of the user's tuning re-reads the
// notes without a second pass over the audio:
//
//   analysePitch    live's pitch estimator, frame by frame on the analysis grid: the strongest harmonic
//                   series (the fundamental plus four partials weighted 1/h) between 80 Hz and 1.5 kHz on
//                   a Hann window twice live's frame FFT, centred on the frame instead of ending at it;
//                   its confidence and evenness by live's formulas. No continuity weighting: the next step
//                   sees the whole line at once.
//   analyseMelody   the notes. A reading an octave off the median of the quarter second around it is that
//                   median's octave (live holds an octave slip for 0.25 s after the fact); a centred
//                   3-frame median steadies the line; frames round to notes with live's 0.75-semitone
//                   hysteresis; a gap under 0.15 s keeps the note (live forgets it after 0.15 s). A note
//                   shorter than live's hold (the tuning's noteHold, longer for a leap) joins the note
//                   before it, or the one after at the start of a line, so a note starts where it starts,
//                   not a hold later as live has to.
//
// Events, each at its note's first frame: noteChange (every note, strength the confidence there), legato
// (a change within a line with no attack from 80 ms before to 50 ms after it; an attack is a jump in the
// mix's level, which a drum hit also is), reattack (a level jump inside a held note, 0.1 s
// after its start and after the last reattack). Continuous values follow
// live's meanings with centred smoothing: pitchHeight ±80 ms, evenness ±0.1 s, glide (the slope of the
// line within a note, semitones a second over 24) ±0.1 s, vibrato (the distance from the note's own
// ±0.15 s centre, half a semitone reads 1) ±0.15 s, runDensity (note changes a second over 12, a
// two-sided 1 s kernel instead of live's decay). pitchClass, pitchHeight, interval and evenness hold
// through unvoiced frames, as live's do; before the first note they read the first note's values.
struct PitchFrames {
    double frameRate = 0.0;
    std::vector<float> midi, confidence, evenness;
};

PitchFrames analysePitch(const float* mono, size_t samples, int sampleRateHz, int hop, int frames);

struct MelodyTrack {
    double frameRate = 0.0;
    std::vector<float> pitch, pitchClass, pitchHeight, confidence, interval, glide, vibrato, evenness, runDensity;
    std::vector<double> notes;  // note change times, seconds, ascending
    std::vector<float> noteStrength;
    std::vector<double> legatos;
    std::vector<float> legatoStrength;
    std::vector<double> reattacks;
    std::vector<float> reattackStrength;
};

// `pitchThreshold`, `noteHold` and `silenceDb` are GeodeSoundTuning's.
MelodyTrack analyseMelody(const PitchFrames& pitch, const Features& features, float pitchThreshold, float noteHold,
                          float silenceDb);

// pitch, pitchClass, pitchHeight, pitchConfidence, interval, glide, vibrato, evenness, runDensity,
// noteChange, legato and reattack, with SoundFrame.h's meanings. Leaves every other field as it is.
void sampleMelody(const MelodyTrack& melody, double seconds, double dt, GeodeSoundFrame& out);

}  // namespace geode::sound::offline
