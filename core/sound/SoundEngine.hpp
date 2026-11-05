#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "sound/FilterBank.hpp"
#include "sound/Fft.hpp"
#include "sound/Form.hpp"
#include "sound/Harmony.hpp"
#include "sound/Rhythm.hpp"
#include "sound/SoundFrame.h"
#include "sound/Stages.hpp"

namespace geode::sound {

// The sound engine: MilkDrop's architecture and TouchDesigner's side by side, both on the sample clock.
//
// MilkDrop: audio is written into a ring as it arrives, and on every analysis hop the engine reads the
// newest window out of it, transforms it and sums the spectrum into bands measured against their own
// running means (bass, mid, treble, their att envelopes, everything spectral, the waveform).
//
// TouchDesigner: every sample also runs through a filter bank as it is written (4-pole bands with a
// power envelope), and each hop reduces the slice since the last hop to one level per band, which then
// goes through Lag and Trigger (low, middle, high and the drums).
//
// Two hops, both counted in written samples, never in frames, so the whole analysis is a function of the
// audio alone: the same song gives the same numbers at 30, 60 or 120 fps, live or in an export (fosfora's
// HopAnalyzer; docs/research/2026-11-05-fosfora-structure-plan.md step 4).
//
//  - Rhythm's 10 ms hop (Rhythm::kHopSeconds), for its evenly sampled onset envelope, as before.
//  - The analysis hop, kAnalysisHz = 60 a second of audio (800 samples at 48 kHz, 735 at 44.1 kHz): the
//    FFT path and everything after it. 60 because every detector's constants were tuned on 60 fps
//    frames: each window is one noisy reading, and the triggers, peaks and sections were set for that
//    many readings a second, not for any rate (at 100 or 120 a second the same code loses the drop and
//    the sections; musical_test fails). So 60 fps behaves exactly as it did, and 30 and 120 fps now
//    behave like it instead of drifting. A 10 ms hop for everything waits on re-tuning those detectors.
//
//   write() ─┬► ring
//            └► filter bank (per sample) ─┬► every 10 ms: band powers ─► Rhythm (tempo, beat, bar, phrase)
//                                         └► every 1/60 s: analyzeHop(): newest window ─► Hann ─► FFT
//                                              ─► tilt ─► bands, descriptors, drums, events, melody, clocks
//                                              ─► Harmony (chroma, key, tension) ─► Form (sections, drop)
//                                              ─► the hop's frame, triggers latched until the next step()
//   step(dt) ─► the newest hop's frame, with every trigger any hop since the last step() fired
//
// Values are held at the newest hop (at most 1/60 s old); interpolating between hops is the next step.
// dt matters to step() only when no audio arrives (a paused or starved live source): then it runs silent
// hops on the frame clock, so the levels still fall and silence still fires, as they did before.
//
// The export path writes exactly the audio up to each video frame and steps, through this same code; one
// engine per surface (screen, wallpaper, export), never shared between threads.
class SoundEngine {
public:
    explicit SoundEngine(int sampleRateHz);

    // About 40 ms of audio at any rate: 2048 at 44.1/48 kHz, 4096 at 88.2/96, 8192 at 176.4/192.
    static int fftSizeFor(int sampleRateHz);
    static GeodeSoundTuning defaultTuning();

    int sampleRateHz() const { return sampleRateHz_; }

    // Analysis hops per second of audio; one hop of written samples is one step of every stage but Rhythm.
    static constexpr int kAnalysisHz = 60;
    static int analysisHopFor(int sampleRateHz);
    int analysisHop() const { return analysisHop_; }

    // Interleaved float PCM; mono feeds both channels, channels past the second are ignored. Runs the
    // analysis on every hop the samples complete.
    void write(const float* interleaved, int frames, int channels);
    // The newest hop's frame; each trigger is the strongest any hop since the last step() fired, so a beat
    // between two frames is never lost and never reported twice.
    void step(float dtSeconds, GeodeSoundFrame& out);
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
        std::array<RelativeLevel, 3> bandFlux;  // low, mid, high
        RelativeLevel hfc;
    };

    BinRange bins(float loHz, float hiHz) const;
    static float sum(const std::vector<float>& mag, BinRange r);
    void readWindow(int count, float* mono, float* left, float* right) const;
    void analyzeHop(float dt);
    void latch(const GeodeSoundFrame& hop);
    float nextRandom();
    void levels(float dt, bool warm, bool silent, GeodeSoundFrame& out);
    void spectrumPoints(float dt, float* out);
    void waveform(float* out);
    void descriptors(float dt, bool warm, bool silent, GeodeSoundFrame& out);
    void drums(float dt, bool silent, GeodeSoundFrame& out);
    void events(float dt, bool warm, GeodeSoundFrame& out);
    void melody(float dt, GeodeSoundFrame& out);
    void clocks(float dt, GeodeSoundFrame& out);
    void musical(float dt, bool warm, bool silent, GeodeSoundFrame& out);
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
    float starvedHopDue_ = 0.0f;  // frame time not yet spent on silent hops while starved
    std::array<SliceBand, kSlices> bank_;
    std::array<float, kSlices> sliceNow_{};   // this frame's slice levels
    std::array<float, kSlices> punchNow_{};   // and the highest punch each slice held
    std::array<int64_t, kSlices> punchAt_{};  // and when, in samples

    // The newest hop's frame, and what step() hands out: that frame with the triggers of every hop since.
    GeodeSoundFrame hopFrame_{};
    GeodeSoundFrame latched_{};

    // Per-hop scratch, overwritten every hop.
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

    // The musical layer.
    Rhythm rhythm_;
    Harmony harmony_;
    Form form_;
    int hopFill_ = 0;  // samples written since the rhythm's last hop
    int analysisHop_ = 800;   // samples per analysis hop
    float analysisDt_ = 1.0f / kAnalysisHz;
    int analysisFill_ = 0;    // samples written since the last analysis hop

    State state_;
};

}  // namespace geode::sound
