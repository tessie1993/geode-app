#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "sound/AnalysisSettings.hpp"
#include "sound/FilterBank.hpp"
#include "sound/Decimate.hpp"
#include "sound/Emotion.hpp"
#include "sound/Fft.hpp"
#include "sound/Form.hpp"
#include "sound/Harmony.hpp"
#include "sound/LufsMeter.hpp"
#include "sound/Rhythm.hpp"
#include "sound/SoundFrame.h"
#include "sound/Stages.hpp"
#include "sound/Transient.hpp"

namespace geode::sound {

// The sound engine: two analyses side by side, a windowed spectrum and a per-sample filter bank, both on one
// sample clock.
//
// The spectrum: audio is written into a ring as it arrives, and every analysis hop the engine reads the newest
// window out of it, transforms it and sums the spectrum into bands measured against their own running means
// (bass, mid, treble, their att envelopes, everything spectral: the relative levels).
//
// The filter bank: every sample also runs through a filter bank as it is written (4-pole bands with a power
// envelope), and each hop reduces the slice since the last hop to one level per band, which then goes
// through Lag and Trigger (low, middle, high and the drums).
//
// The clock is the samples, not the frame: one analysis per fixed hop of samples. write() counts
// samples, and every hop of them (AnalysisSettings: fftSize / overlap, 512 by default at every rate) it runs one
// whole analysis hop with dt = hop / rate: the filter bank's slice, the window, the FFT, every level and
// descriptor, the drums and events, the melody, then the musical layer (Rhythm, Harmony, Form) and the
// feeling (Emotion). Nothing in the analysis knows the frame rate, so the screen at 60 fps, a phone at 40
// and an export stepping at 1/30 s hear exactly the same values at the same sample:
//
//   write() ─┬► ring
//            └► filter bank (per sample) ─► every hop: band powers ─► Rhythm (tempo, beat, bar, phrase)
//                                                    └► analyse(): window ─► Hann ─► FFT ─► levels, spectrum,
//                                                        descriptors, drums, events, melody, clocks
//                                                        ─► Harmony ─► Form ─► Emotion ─► the hop's frame
//   step(dt) ─► the newest hop's frame, its triggers held since the last step (max), the beat, bar and
//               phrase phases moved on to the frame's own sample, and the waveform (a drawing, per frame)
//
// Why a quarter of the window: one clock serves everything (Rhythm's onset envelope included), so a beat, its
// kick and the frame's levels come from the same instant; every sample is in four windows; and at 10.7 ms
// (48 kHz) to 11.6 ms (44.1 kHz) a frame never reads a value more than one hop old. The step is a sample
// count, never a time: every detector takes the hop's duration at construction and counts its own durations
// in hops of that length, so another FFT size or overlap changes the cadence and nothing else. The cost at
// the default: 94 FFTs (and pitch FFTs) a second at 48 kHz, 86 at 44.1.
//
// step() runs the analysis only when the writer has stopped (> 0.25 s without audio): it feeds silence,
// dt worth, so the levels fall and silence fires instead of the last window being read forever.
//
// One engine per surface (screen, wallpaper, export), never shared between threads. Allocation-free after
// construction.
class SoundEngine {
public:
    explicit SoundEngine(int sampleRateHz, AnalysisSettings settings = {});

    // About 40 ms of audio at the rate analysed: 2048 at 44.1/48 kHz. The engine's window is its settings'.
    static int fftSizeFor(int sampleRateHz);
    // The rate the analysis runs at: the input's, halved while it is 88.2 kHz or more (HalfBand), so
    // 88.2/96 and 176.4/192 kHz are analysed at 44.1/48 kHz.
    static int analysisRateFor(int sampleRateHz);
    static GeodeSoundTuning defaultTuning();

    // The rate written to write(), as constructed (at least 8 kHz).
    int sampleRateHz() const { return inputRateHz_; }

    // Interleaved float PCM; mono feeds both channels, channels past the second are ignored.
    void write(const float* interleaved, int frames, int channels);
    // The newest hop's frame, as of the samples written so far; dt only times the writer's silence.
    void step(float dtSeconds, GeodeSoundFrame& out);
    // step()'s clock alone: the writer's silence is counted and fed, no frame is read. The analysis thread's pass.
    void starve(float dtSeconds);

    // Hops by sample position. A position counts samples at the input rate (sampleRateHz(), before any
    // halving), as handed to write() since construction or reset(): the count a ring of the source's own
    // samples keeps. A hop's position is the count when its last sample was written. The silence step()
    // feeds a starved engine is not written, so hops run on it share the position the writer stopped at.
    //
    // The hop at or before `samplePos`, with the beat, bar and phrase phases moved on to that sample as
    // step() moves them; one older than every kept hop reads the oldest (at its own sample), one past the
    // newest reads the newest. The triggers are that hop's own, not held; the waveform (a drawing step()
    // makes per frame) is empty, and fadeGain reads as step() would now. False before the first hop.
    bool frameAt(int64_t samplePos, GeodeSoundFrame& out) const;
    // The newest and oldest kept hop's position, -1 before the first hop.
    int64_t newestHopSample() const;
    int64_t oldestHopSample() const;
    // Hops kept: kHistorySeconds of them, rounded up to whole hops of this engine's length.
    static constexpr float kHistorySeconds = 1.25f;
    int historyHops() const { return historyHops_; }
    // One kept hop as the analysis computed it: its position, the oscillator its phases move on from
    // (Rhythm::phases), its frame (phases at the hop's own sample, triggers its own) and the feeling after it
    // (its own degree accent, not held). What a reader on another thread needs to answer frameAt itself
    // (sound/hop/AnalysisThread).
    struct Hop {
        int64_t sample = 0;
        Rhythm::Clock clock;
        GeodeSoundFrame frame{};
        Emotion::State emotion{};
    };
    // The kept hop `age` hops before the newest (0 = newest), null past the kept ones.
    const Hop* keptHop(int age) const;
    // Hops run since construction or reset(), the starved ones included (they can share a position).
    int64_t hopsRun() const { return hopsRun_; }
    // HalfBand stages between write() and the analysis: a position difference >> halvings() is analysis samples.
    int halvings() const { return halvings_; }
    // The silence rule fired and still holds (the frame's `silence` trigger was its start).
    bool silent() const { return state_.silent; }
    // The feeling (Emotion), stepped every hop with the frame; its degree accent held like a trigger.
    const Emotion::State& emotion() const { return emotionOut_; }
    // A new play (the auto-director's seed changed): the feeling starts over, the analysis does not.
    void resetEmotion() {
        emotion_.reset();
        emotionOut_ = emotion_.state();
        accentHeld_ = 0.0f;
    }
    // Samples per analysis hop, at the analysis rate: the settings' fftSize / overlap.
    int hopSamples() const { return rhythm_.hopSamples(); }
    // The hop's duration, hopSamples() over the analysis rate: what a consumer counts time in hops by.
    float hopSeconds() const { return hopDt_; }
    const AnalysisSettings& settings() const { return settings_; }
    // The one-hop triggers held from hop to step (kick … drop, transient): > 0 on the frame that follows their hop.
    static constexpr int kTriggers = 24;
    static constexpr double kHitFloorLufs = -45.0;
    // Clamped to sound/Tuning's ranges, whatever the caller hands in (a NaN slot is its default).
    void setTuning(const GeodeSoundTuning& tuning);
    const GeodeSoundTuning& tuning() const { return tuning_; }
    void reset();
    // A track boundary the player named (core/sound/PlayerSignals): the ring's audio runs on unbroken, but
    // the song is new, so the musical memory (Rhythm, Harmony, Form) starts over, the relative levels warm
    // up again, and the next step fires sectionChange at full strength. The silence rule does the same on
    // its own after 2 s of silence, for sources with no player to name their boundaries.
    void trackBoundary();
    // The player's fade gain, carried as the frame's fadeGain from the next step on: clamped to 0..1, and 1
    // when it is not a number. Not the engine's state: reset() keeps it.
    void setFadeGain(float gain);
    // Whether the player writes what this engine hears (core/sound/PlayerSignals; an export's decoded track
    // counts as the player). The player's pauses hold the musical memory, so the 2 s silence re-warm runs
    // only for other sources, and fadeGain reads 1 for them. Off by default: a bare engine guesses at
    // boundaries the way it always has.
    void setPlayerSource(bool on) { playerSource_ = on; }

private:
    struct BinRange {
        int from;
        int to;  // exclusive
    };
    // Bands that run through RelativeLevel, all on the tilted spectrum.
    enum Band { kBass, kMid, kTreble, kSub, kMidHigh, kBands };
    // The filter bank's bands. kAll is unfiltered, for a band's share of the whole.
    enum Slice { kLow, kMiddle, kHigh, kKick, kSnareBody, kSnareNoise, kHat, kAll, kSlices };

    // Everything that changes from hop to hop, so reset() is one assignment and nothing is missed.
    struct State {
        std::array<RelativeLevel, kBands> band;
        std::array<float, 3> shaped{};
        std::array<float, 3> presence{};
        std::array<float, 3> bandTime{};
        float curvedTime = 0.0f;
        float drive = 0.0f;      // the eased magnitude of the music (SoundFrame.h)
        float rise = 0.0f;       // momentary loudness over the short-term one, eased
        float onsetRate = 0.0f;  // onsets per second over ~2 s
        float hitRate = 0.0f;    // kicks and snares per second over ~2 s
        float warmSeconds = 50.0f / 30.0f;  // 50 warm-up frames at a 30 fps reference
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
        Envelope transientEnv;
        float transientSharpness = 0.0f;  // the last heard attack's, held
    };

    BinRange bins(float loHz, float hiHz) const;
    static float sum(const std::vector<float>& mag, BinRange r);
    void readWindow(int count, float* mono, float* left, float* right, int oldest = -1) const;
    void feed(float l, float r);
    void analyse();
    void feedSilence(float dt);
    float nextRandom();
    void levels(float dt, bool warm, bool silent, GeodeSoundFrame& out);
    void spectrumPoints(float dt, bool silent, float* out);
    void waveform(float* out);
    void descriptors(float dt, bool warm, bool silent, GeodeSoundFrame& out);
    void drums(float dt, bool silent, GeodeSoundFrame& out);
    void transients(float dt, bool silent, GeodeSoundFrame& out);
    void events(float dt, bool warm, GeodeSoundFrame& out);
    void melody(float dt, GeodeSoundFrame& out);
    void clocks(float dt, GeodeSoundFrame& out);
    void musical(float dt, bool warm, bool silent, GeodeSoundFrame& out);
    void absolute(float dt, GeodeSoundFrame& out);
    // A kick, snare, onset, transient or hard cut may fire: the gate is open and the momentary loudness is above the
    // floor a hit needs, so a soft swell in a quiet ambient passage cannot fire a full-strength hit.
    bool hitsAllowed() const { return meter_.open() && meter_.momentaryLufs() >= kHitFloorLufs; }
    void estimatePitch(float held, float& midi, float& confidence, float& evenness);
    void rewarm();

    // Configuration, fixed after construction.
    int inputRateHz_;
    int halvings_;     // HalfBand stages between write() and the analysis
    int sampleRateHz_;  // the analysis rate
    AnalysisSettings settings_;
    int n_;
    float binHz_;
    GeodeSoundTuning tuning_;
    std::vector<float> hann_;
    std::vector<float> tilt_;
    std::vector<float> pitchHann_;
    std::array<BinRange, kBands> bandBins_;
    std::array<BinRange, GEODE_SOUND_EQ_BANDS> eqBins_;
    std::array<float, GEODE_SOUND_EQ_BANDS> eqSum_{};  // each EQ band's summed magnitude this hop
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
    LufsMeter meter_;          // absolute loudness and the one silence gate
    float trimGain_ = 1.0f;    // the input trim, linear: analysis only
    std::array<std::array<HalfBand, 2>, 3> decimate_{};  // per halving, per channel
    float dcR_ = 0.0f;         // the DC blocker's pole, from the sample rate
    std::array<float, 2> dcX_{};  // its last input and output, per channel
    std::array<float, 2> dcY_{};
    std::array<SliceBand, kSlices> bank_;
    std::array<float, kSlices> sliceNow_{};   // this hop's slice levels
    std::array<float, kSlices> punchNow_{};   // and the highest punch each slice held
    std::array<int64_t, kSlices> punchAt_{};  // and when, in samples
    Transient transient_;  // per sample on the bank's low, middle and high bands and the whole mix
    double silenceCarry_ = 0.0;  // the fraction of a sample of silence still owed while starved

    // The analysis hop: hopSamples() / sampleRateHz_, set at construction.
    float hopDt_ = 0.0f;
    GeodeSoundFrame hopFrame_{};           // the newest hop's frame
    std::array<float, kTriggers> held_{};  // the triggers since the last step
    Emotion emotion_;
    Emotion::State emotionOut_{};
    float accentHeld_ = 0.0f;
    // Every hop as it was computed, a ring of historyHops_, the newest at historyHead_ - 1.
    int historyHops_ = 1;
    std::vector<Hop> history_;
    int historyHead_ = 0;
    int historyCount_ = 0;
    int64_t hopsRun_ = 0;
    int64_t inputWritten_ = 0;  // samples at the input rate, before any halving

    // Per-hop scratch, overwritten every hop.
    std::vector<float> mono_;
    std::vector<float> left_;
    std::vector<float> right_;
    std::vector<float> windowed_;
    std::vector<float> mag_;  // |X|·2/Σw: a full-scale sine reads 1
    std::vector<float> tilted_;
    // The last fluxLag_ hops' log spectra, so flux compares against the one kFluxLag (1/30 s) old.
    std::vector<float> logMag_;      // this hop's, n/2 + 1
    std::vector<float> prevLogMag_;  // fluxLag_ × (n/2 + 1), oldest at fluxHead_, each maximum-filtered (sound/OnsetFlux)
    std::vector<int> fluxReach_;     // per bin, how far that filter reaches either way
    int fluxLag_ = 1;
    int fluxHead_ = 0;
    int fluxCount_ = 0;
    std::vector<float> pitchMono_;
    std::vector<float> pitchMag_;
    std::vector<float> waveSrc_;  // the newest (points + slack) × block samples, read per step
    std::vector<float> waveRaw_;
    std::vector<float> wavePrev_;

    // The musical layer.
    Rhythm rhythm_;
    Harmony harmony_;
    Form form_;
    int hopFill_ = 0;  // samples written since the last hop
    // trackBoundary() since the last step: that step fires sectionChange. reset() (a seek, a new source)
    // keeps it, since the boundary still began a new song.
    bool boundaryPending_ = false;
    float fadeGain_ = 1.0f;
    bool playerSource_ = false;

    State state_;
};

}  // namespace geode::sound
