#pragma once
#include <cstddef>
#include <vector>

#include "sound/SoundFrame.h"

namespace geode::sound::offline {

// The offline analyser's first slice (#463, ADR 0011): a fitted beat grid and its downbeats, for the whole
// track at once. An export hears the song this way (spec #455 § Analysis); the screen, wallpaper and dream
// keep the causal live engine (core/sound). It is a separate implementation by the owner's decision, not a
// mode of the live detectors: live must predict from what has played, this one may look both ways, and
// sharing code would tie either to the other's constraints. What it shares with live is the output: it
// writes the same GeodeSoundFrame fields with the same meanings (sample()), so routes, Scenes and the
// director read an export exactly as they read the screen.
//
// The method is librosa's (0.11.0, the version tools/analysis_lab pins), ported step for step so the lab's
// goldens can hold it to librosa's own output:
//
//   mono ─► STFT (Hann, centred, zero-padded) ─► |X|² ─► 128 Slaney mel bands ─► power_to_db (top 80 dB)
//        ─► positive first difference, median over bands ─► onset strength      (librosa.onset.onset_strength)
//   onset strength ─► 8 s autocorrelation tempogram, mean ─► × log-normal prior at 120 BPM ─► tempo
//                                                                               (librosa.feature.tempo)
//   onset strength ÷ its std ─► Gaussian local score ─► dynamic programme (Ellis 2007, tightness 100)
//        ─► backtrack from the last strong peak ─► trim weak ends ─► beats       (librosa.beat.beat_track)
//
// Downbeats are not in librosa. The bar's phase is the one of four whose bars differ most from each
// other in their mid-band spectrum (a bar holds one harmony; a bar that straddles a change shares half of
// it with its neighbour), plus the beats' own accent (onset strength and the low bands' onset), which is
// what decides a track with no harmony. 4/4 and one phase for the whole track, as live assumes.
//
// The analysis runs at librosa's default resolution scaled to the rate: a 512-sample hop and a 2048-point
// FFT at 22.05 kHz, 1024 and 4096 at 44.1 and 48 kHz, so about 43 frames a second at any rate.

struct Params {
    int fftSize = 2048;
    int hop = 512;
    int mels = 128;
};

// librosa's 512 / 2048 at 22.05 kHz, times the nearest whole multiple of 22.05 kHz (at least 1).
Params paramsFor(int sampleRateHz);

// A log-power mel spectrogram, frames × mels, row-major by frame.
struct MelSpectrogram {
    int frames = 0;
    int mels = 0;
    std::vector<float> db;        // librosa.power_to_db(melspectrogram), ref 1, amin 1e-10, top_db 80
    std::vector<float> centreHz;  // each mel band's centre
    float at(int frame, int mel) const { return db[static_cast<size_t>(frame) * mels + mel]; }
};

MelSpectrogram melSpectrogram(const float* mono, size_t samples, int sampleRateHz, const Params& params);

// librosa.onset.onset_strength with lag 1: the positive change of each band against the frame before,
// aggregated over the bands from `fromHz` up to `belowHz` (median, as beat_track asks, or mean), delayed so
// frame t describes the audio centred at t. One value per spectrogram frame.
enum class Aggregate { Median, Mean };
std::vector<float> onsetStrength(const MelSpectrogram& mel, const Params& params, Aggregate aggregate,
                                 float fromHz = 0.0f, float belowHz = 1e9f);

// librosa.feature.tempo with its defaults (8 s window, prior centred at 120 BPM, one octave wide, below
// 320 BPM): one tempo for the track, in BPM. 0 for an envelope with no onsets.
double estimateTempo(const std::vector<float>& envelope, int sampleRateHz, int hop, double startBpm = 120.0);

// librosa.beat.beat_track's tracker on a given tempo: beat frames, ascending. Empty for no onsets.
std::vector<int> trackBeats(const std::vector<float>& envelope, double bpm, double frameRate, double tightness = 100.0,
                            bool trim = true);

struct BeatGrid {
    double bpm = 0.0;              // the track's tempo (estimateTempo)
    std::vector<double> beats;     // seconds, ascending
    int beatsPerBar = 4;
    int downbeatPhase = 0;         // beats[downbeatPhase + k · beatsPerBar] are the downbeats
    std::vector<double> downbeats; // seconds, ascending
};

// The whole analysis over a mono track. Deterministic: the same samples give the same grid, bit for bit.
BeatGrid analyse(const float* mono, size_t samples, int sampleRateHz);

// Which of the bar's beats is the downbeat, from beat frames over the spectrogram and the envelope.
int downbeatPhase(const MelSpectrogram& mel, const Params& params, const std::vector<float>& envelope,
                  const std::vector<int>& beatFrames, int beatsPerBar = 4);

// The grid as the live engine's rhythm fields, for the frame that ends at `seconds` and lasted `dt`: bpm,
// beatPhase, beatConf, beat, beatCount, downbeat, barPhase, barCount, phrase, phraseIndex, phrasePhase,
// with the meanings SoundFrame.h gives them. A trigger fires on the one frame whose span (seconds - dt,
// seconds] holds its beat, so any frame rate sees every beat once and at the same song time; the counts
// at a given time do not depend on the frame rate at all. beatConf is 1 inside the grid and 0 outside it
// (before the first beat and after the last), where the phases run on at the track's tempo and the
// triggers stay silent, as live's do while unlocked. Leaves every other field as it is.
void sample(const BeatGrid& grid, double seconds, double dt, GeodeSoundFrame& out);

}  // namespace geode::sound::offline
