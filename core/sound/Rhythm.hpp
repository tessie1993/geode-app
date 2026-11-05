#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "sound/SoundFrame.h"

namespace geode::sound {

// The rhythm half of the musical layer: tempo, beat, bar and phrase, from the filter bank's band powers.
//
// It runs at a fixed hop of 10 ms of audio (441 samples at 44.1 kHz), driven from SoundEngine::write(),
// not from the frame: everything it knows is a function of the samples alone, so the screen at 60 fps,
// a phone dropping to 40 and an export stepping at 1/30 s hear the same beats in the same places. step()
// only reads where the oscillator is now.
//
//   band powers ─► dB against one shared reference ─► positive change (20 ms) ─► mean ─► onset envelope
//      (librosa's onset_strength; each band's log over its own mean still feeds the drum accents)
//      onset envelope ─► minus its local mean ─► 6 s ring ─┬► autocorrelation × tempo prior ─► period
//                                                          └► Fourier at that period, whole bars ─► phase
//      period + phase ─► phase-locked oscillator ─► beats ─► kick/snare accent per beat slot ─► downbeat
//
// The tempogram is Grosche & Müller's autocorrelation tempogram restricted to its best lag, and the
// phase is their predominant local pulse (PLP) read only at the newest instant, so it predicts rather than
// describes; the oscillator is the small PLL every real-time tracker puts on top (Meier, Chiu & Müller,
// TISMIR 2024; aubio's beattracking).
class Rhythm {
public:
    // The filter bank bands it listens to, in SoundEngine::Slice order up to kHat.
    static constexpr int kBands = 7;
    enum Band { kLow, kMiddle, kHigh, kKick, kSnareBody, kSnareNoise, kHat };
    static constexpr float kHopSeconds = 0.01f;

    // A beat the oscillator passed since the last read. Beats tick while unlocked too (at the last
    // tempo), so the form tracker always has a grid; only the frame's triggers wait for a lock.
    struct Tick {
        int64_t index;  // beats so far
        bool downbeat;
        bool phrase;    // the first downbeat of an 8-bar phrase
        bool locked;
    };
    static constexpr int kMaxTicks = 8;

    void configure(int sampleRateHz);
    int hopSamples() const { return hop_; }
    // One hop of band powers (SliceBand::power()), the newest sample's.
    void hop(const std::array<float, kBands>& power);
    // Fills the rhythm fields. `sinceHop` is how many samples were written after the last hop, so the
    // phases land on the frame's own instant; `hitTime` is the fallback clock while unlocked.
    void read(int sinceHop, float hitTime, GeodeSoundFrame& out);
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
    float periodOrDefault() const;
    int slotOf(int64_t beat) const { return static_cast<int>(((beat % 4) + 4) % 4); }

    int sampleRateHz_ = 48000;
    int hop_ = 480;
    float hopHz_ = 100.0f;
    int lagMin_ = 30;   // 200 BPM
    int lagMax_ = 100;  // 60 BPM
    std::vector<float> prior_;  // the tempo prior, per lag
    std::vector<float> ac_;     // scratch: the autocorrelation, per lag up to 2 × lagMax_
    std::array<float, 600> line_{};  // scratch: the ring unrolled, oldest first

    // Onset envelope.
    std::array<float, kBands> bandMean_{};
    std::array<std::array<float, 2>, kBands> bandLog_{};  // the last two hops' log level, for a 20 ms change
    std::array<std::array<float, 2>, kBands> bandDb_{};   // ... and their dB against the shared reference
    float dbRef_ = 0.0f;  // the shared reference: the loudest band's power, released over ~4 s
    float localMean_ = 0.0f;
    int64_t hops_ = 0;
    static constexpr int kRing = 600;  // 6 s
    std::array<float, kRing> ring_{};
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
    std::array<float, 4> slotScore_{};
    int downSlot_ = 0;
    int pendingDownSlot_ = -1;
    int pendingVotes_ = 0;
    int barInPhrase_ = 0;
    int64_t bars_ = 0;
    int64_t phrases_ = 0;
    bool phrasePending_ = false;
    float hopKick_ = 0.0f;   // this hop's kick and snare onset strengths
    float hopSnare_ = 0.0f;

    std::array<Tick, kMaxTicks> ticks_{};
    int tickCount_ = 0;
    // Strongest beat, downbeat and phrase trigger since the last read.
    float beatHit_ = 0.0f;
    float downbeatHit_ = 0.0f;
    float phraseHit_ = 0.0f;
};

}  // namespace geode::sound
