#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "sound/FilterBank.hpp"
#include "sound/Fft.hpp"
#include "sound/Harmony.hpp"
#include "sound/SoundFrame.h"
#include "sound/Stages.hpp"

namespace geode::sound {

// The sound engine: MilkDrop's architecture and TouchDesigner's side by side, both frame-locked.
//
// MilkDrop: audio is written into a ring as it arrives, and once per rendered frame the engine reads the
// newest window out of it, transforms it and sums the spectrum into bands measured against their own
// running means (bass, mid, treble, their att envelopes, everything spectral, the waveform).
//
// TouchDesigner: every sample also runs through a filter bank as it is written (4-pole bands with a
// power envelope), and each frame reduces the slice since the last frame to one level per band, which
// then goes through Lag and Trigger (low, middle, high and the drums).
//
//   write() ─┬► ring ──► step(dt): newest window ─► Hann ─► FFT ─► tilt ─► bands ─► relative levels
//            │                                  └► waveform, descriptors, melody, EQ, spectrum
//            └► filter bank (per sample) ─► step(dt): slice levels ─► lag, threshold, gain ─► low/middle/high
//                                                                  └► punch ─► triggers ─► envelopes
//
// No clock of its own and no hop: the frame is the unit, and each stage scales its smoothing by dt.
// The export path writes exactly the audio up to each video frame and steps with dt = 1/fps, through
// this same code; one engine per surface (screen, wallpaper, export), never shared between threads.
class SoundEngine {
public:
    explicit SoundEngine(int sampleRateHz);

    // About 40 ms of audio at any rate: 2048 at 44.1/48 kHz, 4096 at 88.2/96, 8192 at 176.4/192.
    static int fftSizeFor(int sampleRateHz);
    static GeodeSoundTuning defaultTuning();

    int sampleRateHz() const { return sampleRateHz_; }
    int fftSize() const { return n_; }

    // Interleaved float PCM; mono feeds both channels, channels past the second are ignored.
    void write(const float* interleaved, int frames, int channels);
    void step(float dtSeconds, GeodeSoundFrame& out);
    // The harmony stage's frame for the last step(): pitch-class profiles, the tonal centre, mode and
    // tension (sound/Harmony). Kept beside GeodeSoundFrame rather than in it, so the frame's wire layout,
    // lib_sound and the reaction sources stay as they are; the colour engine reads it on the GL thread.
    const GeodeHarmony& harmony() const { return harmony_.frame(); }
    void setTuning(const GeodeSoundTuning& tuning) { tuning_ = tuning; }
    const GeodeSoundTuning& tuning() const { return tuning_; }
    void reset();

private:
    struct BinRange {
        int from;
        int to;  // exclusive
    };
    // Bands that run through RelativeLevel, all on the tilted spectrum.
    enum Band { kBass, kMid, kTreble, kSub, kMidHigh, kBands };
    // The filter bank's bands. kAll is unfiltered, for a band's share of the whole.
    enum Slice { kLow, kMiddle, kHigh, kKick, kSnareBody, kSnareNoise, kHat, kAll, kSlices };

    // Everything that changes from frame to frame, so reset() is one assignment and nothing is missed.
    struct State {
        std::array<RelativeLevel, kBands> band;
        std::array<float, 3> shaped{};
        std::array<float, 3> presence{};
        std::array<float, 3> bandTime{};
        float curvedTime = 0.0f;
        float levelPeak = 1e-4f;
        float intensity = 0.0f;
        float warmSeconds = 50.0f / 30.0f;  // MilkDrop's 50 warm-up frames at its 30 fps reference
        float quietSeconds = 0.0f;
        std::array<RelativeLevel, GEODE_SOUND_EQ_BANDS> eq;
        float spectrumPeak = 1e-6f;
        bool waveHasPrev = false;
        RelativeLevel flux;
        float centroid = 0.0f;
        float rolloff = 0.0f;
        float flatness = 0.0f;
        float densitySlow = 0.0f;
        float densityFast = 0.0f;
        float energyShort = 0.0f;
        float energyLong = 0.0f;
        float peakFreq = 0.0f;
        float peakMagPeak = 1e-6f;
        float peakMag = 0.0f;
        float width = 0.0f;
        float pan = 0.0f;
        std::array<RelativeLevel, 3> slice;  // low, middle, high
        std::array<Trigger, 3> drumTrigger;
        int64_t kickPeakAt = -1;  // the last strong kick-band punch, in samples, and how strong
        float kickPeakPunch = 0.0f;
        std::array<Envelope, 4> envelope;  // kick, snare, hat, onset
        uint32_t kickCount = 0;
        uint32_t snareCount = 0;
        std::array<float, 4> easedTarget{};  // kick toggle, snare toggle, kick random, note random
        std::array<float, 4> eased{};
        uint32_t random = 0x9E3779B9u;
        HardCut hardCut;
        Trigger onset;
        bool silent = false;
        float belowSeconds = 0.0f;
        float silentSeconds = 0.0f;
        bool surgeArmed = true;
        float quietMemory = 0.0f;
        float sinceAttack = 1e9f;
        float sinceReattack = 1e9f;
        int candidateNote = -1;
        float candidateSeconds = 0.0f;
        int note = -1;
        float lostSeconds = 0.0f;
        float octaveSeconds = 0.0f;
        float lastMidi = 0.0f;
        float meanMidi = 0.0f;
        float pitchClass = 0.0f;
        float pitchHeight = 0.0f;
        float interval = 0.0f;
        float glide = 0.0f;
        float vibrato = 0.0f;
        float evenness = 0.0f;
        float runRate = 0.0f;
        float hitCount = 0.0f;  // whole hits so far; the eased clock chases it
        float hitTime = 0.0f;
        float noteCount = 0.0f;
        float noteTime = 0.0f;
        float presenceFade = 0.0f;
    };

    BinRange bins(float loHz, float hiHz) const;
    static float sum(const std::vector<float>& mag, BinRange r);
    void readWindow(int count, float* mono, float* left, float* right) const;
    float nextRandom();
    void levels(float dt, bool warm, bool silent, GeodeSoundFrame& out);
    void spectrumPoints(float dt, float* out);
    void waveform(float* out);
    void descriptors(float dt, bool warm, bool silent, GeodeSoundFrame& out);
    void drums(float dt, bool silent, GeodeSoundFrame& out);
    void events(float dt, bool warm, GeodeSoundFrame& out);
    void melody(float dt, GeodeSoundFrame& out);
    void clocks(float dt, GeodeSoundFrame& out);
    void estimatePitch(float held, float& midi, float& confidence, float& evenness);

    // Configuration, fixed after construction.
    int sampleRateHz_;
    int n_;
    float binHz_;
    GeodeSoundTuning tuning_;
    std::vector<float> hann_;
    std::vector<float> tilt_;
    std::vector<float> pitchHann_;
    std::array<BinRange, kBands> bandBins_;
    std::array<BinRange, GEODE_SOUND_EQ_BANDS> eqBins_;
    std::array<BinRange, GEODE_SOUND_SPECTRUM_POINTS> spectrumBins_;
    BinRange audible_{};    // 20 Hz–20 kHz
    BinRange musical_{};    // 100 Hz–10 kHz
    BinRange fluxBins_{};   // 30 Hz–16 kHz
    std::vector<float> waveWeight_;
    int waveBlock_ = 4;
    Fft fft_;
    Fft pitchFft_;  // twice as long, for pitch resolution down to 80 Hz

    // The ring: two channels, newest sample at writePos_ - 1.
    std::vector<float> ringL_;
    std::vector<float> ringR_;
    size_t writePos_ = 0;
    int64_t written_ = 0;
    bool fresh_ = false;
    float starvedSeconds_ = 0.0f;
    bool starved_ = false;  // nothing written for a while: the window reads as silence, not a frozen loop
    std::array<SliceBand, kSlices> bank_;
    std::array<float, kSlices> sliceNow_{};   // this frame's slice levels
    std::array<float, kSlices> punchNow_{};   // and the highest punch each slice held
    std::array<int64_t, kSlices> punchAt_{};  // and when, in samples

    // Per-frame scratch, overwritten every step.
    std::vector<float> mono_;
    std::vector<float> left_;
    std::vector<float> right_;
    std::vector<float> windowed_;
    std::vector<float> mag_;  // |X|·2/Σw: a full-scale sine reads 1
    std::vector<float> tilted_;
    // The last few frames' log spectra, so flux always compares against one about kFluxLag old: against
    // the previous frame it would measure half the change at 60 fps that it does at 30.
    static constexpr int kFluxSlots = 9;  // enough for kFluxLag at 240 fps
    std::vector<float> prevLogMag_;       // kFluxSlots × (n/2 + 1)
    std::array<float, kFluxSlots> fluxAge_{};  // seconds; < 0 = empty
    int fluxHead_ = 0;
    std::vector<float> pitchMono_;
    std::vector<float> pitchMag_;
    std::vector<float> waveRaw_;
    std::vector<float> wavePrev_;

    State state_;
    Harmony harmony_;
};

}  // namespace geode::sound
