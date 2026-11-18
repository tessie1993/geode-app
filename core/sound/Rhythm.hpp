#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "sound/SoundFrame.h"

namespace geode::sound {

// The rhythm half of the musical layer: tempo, beat, bar and phrase, from the filter bank's band powers.
//
// It runs on the sound engine's analysis hop (a sample count, sound/AnalysisSettings), driven from
// SoundEngine::write(), not from the frame: everything it knows is a function of the samples alone, so the
// screen at 60 fps, a phone dropping to 40 and an export stepping at 1/30 s hear the same beats in the same
// places. configure() turns every duration below into hops of the hop's true length; SoundEngine::step()
// only reads where the oscillator is now.
//
//   band powers ─► level against one shared reference ─► positive change (20 ms) ─► mean ─► onset envelope
//      (each band's log over its own mean still feeds the drum accents)
//      onset envelope ─► minus its local mean ─► 6 s ring ─┬► normalised autocorrelation ─► period
//                                                          └► Fourier at that period, whole bars ─► phase
//      period + phase ─► phase-locked oscillator ─► beats ─► kick/snare accent per beat ─► meter, downbeat
//
// The tempo comes from the music alone. No tempo is preferred, weighted or assumed: every lag in the
// search range is scored by the same normalised autocorrelation, the beat is the peak whose periodicity
// times its pulse (the envelope's Fourier coefficient at that period, one window for every lag) is the
// highest (evidence alone: neither a tempo nor the order of the lags decides), and until one is found
// there is no period, no beat and no tick (bpm 0). tools/no-bpm-bias-gate.sh and musical_test's tempo
// range keep it so.
//
// The phase is Grosche & Müller's predominant local pulse (PLP) read only at the newest instant, so it
// predicts rather than describes; the oscillator is the small PLL every real-time tracker puts on top
// (Meier, Chiu & Müller, TISMIR 2024).
class Rhythm {
public:
    // The filter bank bands it listens to, in SoundEngine::Slice order up to kHat.
    static constexpr int kBands = 7;
    enum Band { kLow, kMiddle, kHigh, kKick, kSnareBody, kSnareNoise, kHat };
    // The search range, flat inside: what the hop and the 6 s ring can measure, not what music is
    // expected to be. The fastest beat is 0.2 s, the slowest 2 s; the ring holds kPeriodsHeard of it.
    static constexpr float kFastestBpm = GEODE_SOUND_BPM_FASTEST;
    static constexpr int kPeriodsHeard = 3;
    static constexpr float kSlowestBpm = GEODE_SOUND_BPM_SLOWEST;

    // A beat the oscillator passed since the last read. Beats tick while unlocked too (at the last
    // tempo heard), so the form tracker has a grid; only the frame's triggers wait for a lock. Before
    // the first tempo is heard nothing ticks.
    struct Tick {
        int64_t index;  // beats so far
        bool downbeat;
        bool phrase;    // the first downbeat of an 8-bar phrase
        bool locked;
    };
    static constexpr int kMaxTicks = 8;

    // `hopSamples` at `sampleRateHz`: every count below is derived from the hop's duration here.
    void configure(int sampleRateHz, int hopSamples);
    int hopSamples() const { return cfg_.hop; }
    // One hop of band powers (SliceBand::takeHopPower()), each the mean over the hop.
    void hop(const std::array<float, kBands>& power);
    // Fills the rhythm fields. `sinceHop` is how many samples were written after the last hop, so the
    // phases land on the frame's own instant; `hitTime` is the fallback clock while unlocked.
    void read(int sinceHop, float hitTime, GeodeSoundFrame& out);
    // Only the beat, bar and phrase phases, `sinceHop` samples after the last hop: the sound engine reads
    // the rest at the hop and moves these on to each frame's own sample.
    void phases(int sinceHop, float hitTime, GeodeSoundFrame& out) const;
    // What phases() reads of the oscillator, kept per hop so a past hop's phases can be moved on later.
    struct Clock {
        double theta = 0.0;
        float period = 0.0f;
        int64_t lastBeat = 0;
        int hop = 0;  // samples per hop, as configured
        int meter = 4;
        int downSlot = 0;
        int barInPhrase = 0;
        bool locked = false;
    };
    Clock clock() const { return {theta_, period_, lastBeat_, cfg_.hop, meter_, downSlot_, barInPhrase_, locked_}; }
    static void phases(const Clock& clock, int sinceHop, float hitTime, GeodeSoundFrame& out);
    // The beats passed since the last read, oldest first (read() empties it).
    int ticks(const Tick*& out) const {
        out = ticks_.data();
        return tickCount_;
    }
    // The positive change of the low, middle and high bands since the last read (their highest hop), for
    // fluxLow/Mid/High, then starts over.
    std::array<float, 3> takeBandFlux();

    // Call after the frame's ticks and triggers are read: the next frame collects its own.
    void clearFrame();

    // From the form tracker. A section began `barsAgo` bars ago (0 = on this downbeat): the phrase count
    // restarts there. A drop also says where the downbeat is: the beat nearest now.
    void sectionStarted(int barsAgo);
    void dropHere();
    // A harmonic change near a beat makes that beat slot more likely the downbeat (chords change on bars).
    void harmonyAccent(float strength);

    bool locked() const { return locked_; }
    void reset();

private:
    void estimateTempo();
    void measurePhase();
    void advance();
    void finishBeatAccent(int64_t beat);
    void snapPhase();
    void measureMeter(int64_t beat);
    int slotOf(int64_t beat) const { return static_cast<int>(((beat % meter_) + meter_) % meter_); }

    // Fixed by configure(): the hop and every count derived from its duration.
    struct Config {
        int sampleRateHz = 0;
        int hop = 0;              // samples
        float hopSeconds = 0.0f;  // hop / rate
        float hopHz = 0.0f;
        int lagMin = 0;      // kFastestBpm's period, rounded down to hops
        int lagMax = 0;      // kSlowestBpm's, rounded up
        int ring = 0;        // kPeriodsHeard × lagMax: the slowest beat fits that many times
        int tempoAfter = 0;  // hops before the first tempo estimate
        int phaseWindow = 0; // hops the phase reads at most
        int change = 1;      // hops the onset change spans
        float latencyHops = 0.0f;
    };
    Config cfg_;
    std::vector<float> ac_;       // scratch: the autocorrelation, per lag up to 2 × lagMax
    std::vector<float> line_;     // scratch: the ring unrolled, oldest first
    std::vector<float> squares_;  // scratch: its running energy

    // Onset envelope.
    std::array<float, kBands> bandMean_{};
    // The last cfg_.change hops' log level per band (band-major), for the change over that span, ...
    std::vector<float> bandLog_;
    std::vector<float> bandLevel_;  // ... and their level against the shared reference
    int changeHead_ = 0;            // the slot written cfg_.change hops ago, written next
    float dbRef_ = 0.0f;  // the shared reference: the loudest band's power, released over ~4 s
    float localMean_ = 0.0f;
    int64_t hops_ = 0;
    float sinceTempo_ = 0.0f;  // seconds gathered toward the next tempo estimate (Stages due())
    float sincePhase_ = 0.0f;  // ... and the next phase measurement
    std::vector<float> ring_;  // the onset envelope, cfg_.ring hops (6 s)
    int ringHead_ = 0;  // the next slot to write
    std::array<float, 3> bandFluxPeak_{};

    // Tempo: the period in hops, the candidate that would replace it, and how sure the tempogram is.
    float period_ = 0.0f;        // 0 = none yet
    float candidate_ = 0.0f;
    int candidateVotes_ = 0;
    float acConf_ = 0.0f;        // the autocorrelation peak over the energy, 0..1
    float plpConf_ = 0.0f;       // how much of the onset mass sits at one phase of that period, 0..1
    float conf_ = 0.0f;          // both, smoothed (~1 s)
    float lockSeconds_ = 0.0f;   // how long conf has stayed past the lock (or unlock) line
    float beatlessSeconds_ = 0.0f;  // unlocked and under the unlock line this long: at a whole window the tempo is let go
    bool locked_ = false;
    bool everLocked_ = false;

    // The oscillator: beats as a running count, its fraction the beat phase.
    double theta_ = 0.0;
    double pending_ = 0.0;     // phase correction still to spread over the next hops
    float measured_ = 0.0f;    // the PLP phase at the last measurement, 0..1
    float missSeconds_ = 0.0f; // how long the measurement has disagreed by more than a quarter beat
    int64_t lastBeat_ = 0;

    // Downbeat: the kick and snare accents heard around each beat slot of the bar, leaky over ~6 bars.
    int64_t accentBeat_ = 0;  // the beat the accumulators below are collecting for
    float kickAccent_ = 0.0f;
    float snareAccent_ = 0.0f;
    float kickMean_ = 0.0f;   // the typical beat's accents, so a slot scores what sets it apart
    float snareMean_ = 0.0f;
    bool accentSeeded_ = false;
    std::array<float, 4> slotScore_{};
    // Meter: each beat's accent (as slotScore_ adds it) over the last kMeterBeats, indexed by beat, and how
    // much of their spread a pattern repeating every 3 or every 4 beats explains. The bar is 4 beats until
    // the music shows 3, and changes only after a run of beats that all say so.
    static constexpr int kMeterBeats = 24;  // whole bars of 3 and of 4
    std::array<float, kMeterBeats> beatAccent_{};
    int accentBeats_ = 0;  // consecutive beats in beatAccent_
    int meter_ = 4;
    int meterVotes_ = 0;
    int downSlot_ = 0;
    bool downHeard_ = false;  // the bar has been placed from accents since the last lock
    int pendingDownSlot_ = -1;
    int pendingVotes_ = 0;
    int barInPhrase_ = 0;
    int64_t bars_ = 0;
    int64_t phrases_ = 0;
    bool phrasePending_ = false;
    // The accent bands' log levels this hop (kKick, kLow, kSnareBody, kSnareNoise), and around the current
    // beat each one's lowest level so far and its largest rise from it.
    static constexpr float kNoLevel = 1e30f;
    std::array<float, 4> hopLog_{};
    std::array<float, 4> accentLow_{kNoLevel, kNoLevel, kNoLevel, kNoLevel};
    std::array<float, 4> accentRise_{};

    std::array<Tick, kMaxTicks> ticks_{};
    int tickCount_ = 0;
    // Strongest beat, downbeat and phrase trigger since the last read.
    float beatHit_ = 0.0f;
    float downbeatHit_ = 0.0f;
    float phraseHit_ = 0.0f;
};

}  // namespace geode::sound
