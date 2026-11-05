#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "sound/SoundFrame.h"
#include "sound/offline/OfflineBeats.hpp"
#include "sound/offline/OfflineFeatures.hpp"
#include "sound/offline/OfflineForm.hpp"
#include "sound/offline/OfflineHarmony.hpp"
#include "sound/offline/OfflineMelody.hpp"

namespace geode::sound::offline {

// The whole offline analysis of one track, and the one call that reads it into a GeodeSoundFrame (#464,
// ADR 0011). It runs the slices before it (OfflineBeats' grid, OfflineFeatures' frame features,
// OfflineForm's sections (#465), OfflineHarmony, OfflineMelody) and adds the fields none of them write, so
// that every field of the live schema has an offline value.
//
// What this file adds, each with live's meaning and range and the centred method that replaces live's
// causal one:
//   low, middle, high   bass/mid/treble's relative levels through the user's lag, threshold, gain and
//                       offset; the lag runs forwards and backwards and the two are averaged, so a hit
//                       peaks on its own frame instead of an attack later
//   presences           each band's att over a centred ±3 s (live: a 3 s follower), halved and clamped
//   intensity           live's creep (up 0.15 a second per unit the 0.4 s loudness passes the 8 s one, down
//                       3 % a second), run forwards and backwards on centred loudness and averaged
//   clocks              bassTime … curvedTime integrate the centred att levels on the analysis grid and are
//                       read between frames; hitTime and noteTime count kicks + snares and note changes,
//                       each eased over a centred 150 ms; presence is the share of the centred ±1 s above
//                       the silence level
//   densitySlow/Fast    live's partial count (peaks over a tenth of the 100 Hz–10 kHz maximum, over 48) on a
//                       window of live's size centred on the frame, over ±0.5 s and ±50 ms
//   stereoWidth/Pan     side over mid + side, and right minus left, from the centred ±0.1 s energies
//   kickRandom          live's random sequence (the same generator and seed), drawn on each kick and note
//   noteRandom          change in time order as live draws them, eased over 60 ms
//   hardCut             live's rule (the unbounded mean relative level over a threshold that doubles on each
//                       cut and relaxes with the tuning's half-life, at most one a second), firing on the
//                       peak of the rise (up to 0.1 s on) rather than its first frame over
//   silence, returned   the music stopping, at the moment it stops (live knows half a second later), and
//                       coming back after at least a second, with live's hysteresis (under the tuning's
//                       silence level, back above twice it); a track that opens silent has stopped nothing
//   surge               the 0.4 s after a frame against the 8 s before it passing 1.6, a quarter-second
//                       local peak, after a passage under 0.75 of it within 4 s and not within a second of a
//                       return; re-armed below 1.1, as live
//   waveform            the newest ~13 ms at the frame's time, block-averaged as live; the alignment is a
//                       trigger (the slide over the slack whose middle crosses zero rising, nearest zero)
//                       rather than a match to the frame before, so it needs no state
//
// The user's tuning moves low/middle/high, the clocks' speed, the silence level, the hard cut and the
// melody's thresholds: applyTuning() re-reads those from the cached analysis without touching the audio.

struct Track {
    int sampleRateHz = 0;
    double frameRate = 0.0;
    int frames = 0;
    BeatGrid grid;
    FormTimeline form;
    Features features;
    HarmonyTrack harmony;
    PitchFrames pitch;

    // From the audio, whatever the tuning.
    std::vector<float> densitySlow, densityFast, stereoWidth, stereoPan, intensity;
    std::array<std::vector<float>, 3> presence;  // bass, mid, treble
    std::vector<double> rawVol;                   // the unbounded mean relative level, for the hard cut
    std::vector<double> energy;                   // rms² per frame
    int waveBlock = 1;
    std::vector<int16_t> wave;                    // block-averaged mono, full scale = 32767

    // From the tuning (applyTuning).
    GeodeSoundTuning tuning{};
    MelodyTrack melody;
    std::array<std::vector<float>, 3> shaped;      // low, middle, high
    std::array<std::vector<double>, 3> bandClock;  // running integrals, frames + 1
    std::vector<double> curvedClock;
    std::vector<float> presenceFade;
    std::vector<double> hits;  // kicks and snares, ascending
    std::vector<double> kickRandomAt, noteRandomAt;
    std::vector<float> kickRandom, noteRandom;
    std::vector<double> hardCuts, silences, returns, surges;
    std::vector<float> surgeStrength;
};

// SoundEngine::defaultTuning's values, here so the offline files build without the live engine.
GeodeSoundTuning offlineDefaultTuning();

// Interleaved float PCM; mono feeds both channels, channels past the second are ignored (as live).
Track analyseTrack(const float* interleaved, size_t frames, int channels, int sampleRateHz);
Track analyseTrack(const float* interleaved, size_t frames, int channels, int sampleRateHz, const GeodeSoundTuning& tuning);

void applyTuning(Track& track, const GeodeSoundTuning& tuning);

// Every GeodeSoundFrame field for the video frame that ends at `seconds` and lasted `dt`. Triggers fire on
// the one frame whose span (seconds - dt, seconds] holds them.
void sampleTrack(const Track& track, double seconds, double dt, GeodeSoundFrame& out);

}  // namespace geode::sound::offline
