#pragma once
#include <array>
#include <cstddef>
#include <vector>

#include "sound/SoundFrame.h"

namespace geode::sound::offline {

// The offline analyser's second slice (#464, ADR 0011): the frame features, for the whole track at once.
// It covers bands and levels on centred windows, onsets (plain and whitened), harmonic/percussive
// separation and the drums found in the percussive part, CQT chroma, and the key. Like OfflineBeats, it
// is written fresh rather than as a mode of the live detectors, and it writes the live GeodeSoundFrame
// fields with their meanings (sampleFeatures()).
//
// Methods, each the librosa 0.11.0 function it names (the lab's goldens hold the raw values to librosa):
//   rms                      librosa.feature.rms(frame_length = FFT size, centred)
//   centroid, rolloff        librosa.feature.spectral_centroid / spectral_rolloff (85 %) on |STFT|
//   flatness                 librosa.feature.spectral_flatness (power 2, amin 1e-10)
//   onset strength           librosa.onset.onset_strength (mean over mel bands), and the onsets
//                            librosa.onset.onset_detect picks from it (peakPick())
//   harmonic / percussive    librosa.decompose.hpss on |STFT|: 31-frame and 31-bin medians, power-2 soft masks
//   chroma                   librosa.feature.chroma_cqt with tuning 0: 252 CQT bins (C1, 36 a semitone…
//                            36 an octave) folded to 12. librosa computes each octave from a soxr-resampled
//                            signal; here a 63-tap windowed-sinc halves the rate, so values are close, not equal
// What librosa does not have, built on those:
//   whitened onsets          Stowell & Plumbley's adaptive whitening (ICMC 2007) of each mel band before the
//                            flux: every band is measured against its own recent peak, so a quiet instrument's
//                            note starts register beside a loud one's. The onset trigger reads this one.
//   kick, snare, hat         onsets of the percussive part's low (30–150 Hz), body (150–1000 Hz) and air
//                            (7–16 kHz) bands, picked with the same peak picker
//   key                      the track's mean chroma against the Krumhansl–Kessler profiles, as live Harmony
//   bands, eq, spectrum      +6 dB/octave tilted power, each over its own centred 4 s mean (relative) or peak
//
// Analysis frames are OfflineBeats::paramsFor's (about 43 a second, frame t centred on sample t · hop).
// Memory is a few hundred floats per frame; the STFT for the separation streams through a 32-frame ring.

struct Features {
    int sampleRateHz = 0;
    int hop = 0;
    int frames = 0;

    // Raw values, librosa's units (the goldens compare these).
    std::vector<float> rms;
    std::vector<float> centroidHz;
    std::vector<float> rolloffHz;
    std::vector<float> flatness;
    std::vector<float> onsetMean;      // librosa.onset.onset_strength, mean aggregate
    std::vector<float> onsetWhitened;  // the same over adaptively whitened bands
    std::vector<float> harmonic;       // Σ |H|² per frame
    std::vector<float> percussive;     // Σ |P|² per frame
    std::vector<std::array<float, 12>> chroma;  // chroma_cqt, each frame over its maximum

    // Schema values per frame, already on SoundFrame.h's scales (sampleFeatures only reads them).
    std::vector<std::array<float, 5>> band;     // relative: bass, mid, treble, sub, midHigh
    std::vector<std::array<float, 5>> bandAtt;  // the same over ~70 ms
    std::vector<float> level, flux, energyTrend, peakFreq, peakMag, hfc;
    std::vector<std::array<float, 3>> fluxBands;  // relative positive change < 250 Hz, 250 Hz–4 kHz, > 4 kHz
    std::vector<std::array<float, GEODE_SOUND_EQ_BANDS>> eq;
    std::vector<std::array<float, GEODE_SOUND_SPECTRUM_POINTS>> spectrum;
    std::vector<std::array<float, 12>> chromaMedium, chromaFast, chromaSlow;

    // Events, seconds, ascending.
    std::vector<double> onsets;    // from onsetWhitened
    std::vector<double> kicks, snares, hats;

    // The key of the whole track.
    int keyRoot = 0;
    bool keyMajor = true;
    float keyClarity = 0.0f;
};

Features analyseFeatures(const float* mono, size_t samples, int sampleRateHz);

// librosa.util.peak_pick with onset_detect's defaults at this frame rate, on the envelope normalised to
// 0..1 as onset_detect does: a frame that is the maximum of the last 30 ms, at least 0.07 above the mean of
// the 100 ms around it, then 30 ms of quiet. Returns frame indices.
std::vector<int> peakPick(const std::vector<float>& envelope, double frameRate);

// chroma_cqt's CQT, folded and normalised per frame. Exposed for the test.
std::vector<std::array<float, 12>> cqtChroma(const float* mono, size_t samples, int sampleRateHz, int hop, int frames);

// The best of the 24 Krumhansl–Kessler keys for a chroma, and the correlation that won.
void keyOf(const std::array<double, 12>& chroma, int& root, bool& major, float& clarity);

// The features as the live engine's fields, for the frame that ends at `seconds` and lasted `dt`:
//   relative levels  bass, mid, treble, sub, midHigh over their centred 4 s mean, bent so nothing passes
//                    4; *Att the same over ~70 ms; vol / volAtt the mean of the three
//   level            rms over its centred 8 s peak
//   descriptors      rms, loudness, centroid, rolloff, flatness, flux, energyTrend, peakFreq, peakMag on
//                    SoundFrame.h's axes; fluxLow/Mid/High and hfc relative
//   triggers         onset, kick, snare, hat, kick2…kick16, snare2, snare4 on the one frame whose span holds
//                    the event; their envelopes decay from it (the tuning's default decays); the toggles
//                    flip on each hit and ease over 60 ms
//   harmony          keyRoot, keyMajor, keyClarity, keyHue; chroma (~0.5 s), chromaFast (~0.1 s) and
//                    chromaSlow (~4 s), each over its maximum
//   arrays           eq (relative) and spectrum (over its centred 4 s peak)
// Every other field is left as it is (the rhythm is OfflineBeats::sample's; sections are #465's).
void sampleFeatures(const Features& features, double seconds, double dt, GeodeSoundFrame& out);

}  // namespace geode::sound::offline
