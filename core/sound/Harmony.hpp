#pragma once
#include <array>

#include "sound/SoundFrame.h"
#include "sound/Stages.hpp"

namespace geode::sound {

// One entry of the musical intelligence database's scale catalogue (tools/musical_intelligence.py).
struct ScaleInfo {
    const char* id;
    unsigned mask;     // pitch classes above the root, bit 0 = the root
    float brightness;  // -1 dark … 1 bright
    float tension;     // 0 calm … 1 tense
};
int scaleCount();
const ScaleInfo& scaleInfo(int index);

// The harmony half of the musical layer: chroma, key, the chord's colour, tension and harmonic change,
// read every analysis hop (10 ms) from the sound engine's long (pitch) spectrum.
//
//   |X| 250 Hz–2 kHz ─► spectral peaks ─► log over the frame's peak ─► nearest pitch class ─┬► fast (0.1 s), medium (0.5 s), slow (4 s)
//     slow ─► Noland's key profiles (24 keys, Pearson) ─► key, major/minor, clarity
//     medium ─► circle-of-fifths centre (chordHue), interval-class dissonance (tension)
//     fast, every 0.1 s ─► last 0.5 s against the 2 s before ─► harmonic change
//
// The long spectrum (twice the window FFT, ~11 Hz bins at 44.1 kHz) is the melody tracker's; a semitone at
// 250 Hz is 15 Hz wide, so the range starts there. Every smoothing scales with dt, as the rest of the engine.
class Harmony {
public:
    // `binHz`: the long spectrum's bin width; `bins`: how many magnitudes it holds.
    void configure(float binHz, int bins);
    // Fills keyHue … harmonicChange and the three chroma arrays. Silence holds everything.
    void step(const float* mag, float dt, bool silent, GeodeSoundFrame& out);
    void reset();
    // 0..1: how much of the chroma band's power sits on spectral peaks (notes) rather than a smooth
    // spread (a drum's spill). A harmonic change counts toward the bar only as far as it is tonal.
    float tonality() const { return tonality_; }

private:
    void detectScale();

    static constexpr int kSnapshots = 25;  // 2.5 s at 10 a second
    static constexpr float kSnapshotEvery = 0.1f;

    float binHz_ = 10.0f;
    int from_ = 1;  // the bins searched for peaks, 250 Hz–2 kHz
    int to_ = 1;

    std::array<float, 12> fast_{};
    std::array<float, 12> medium_{};
    std::array<float, 12> slow_{};
    bool heard_ = false;
    float tonality_ = 0.0f;
    float heardSeconds_ = 0.0f;  // audio the chromas have averaged, up to their mean-to-average handover
    int key_ = -1;          // 0..11 major on that tonic, 12..23 minor; -1 before any
    float keyHold_ = 0.0f;  // how long the current key has fitted worse than another
    float clarity_ = 0.0f;
    float major_ = 0.5f;
    float chordHue_ = 0.0f;
    float tension_ = 0.0f;
    std::array<std::array<float, 12>, kSnapshots> snapshots_{};
    int snapshotCount_ = 0;
    int snapshotHead_ = 0;
    float sinceSnapshot_ = 0.0f;
    float change_ = 0.0f;
    Trigger trigger_;
    // Scale detection, four times a second on the slow chroma.
    float sinceScale_ = 0.0f;
    int scale_ = -1;
    int scaleRoot_ = 0;
    float scaleHold_ = 0.0f;  // how long the current scale has fitted worse than another
    float scaleFit_ = 0.0f;
    float scaleBright_ = 0.5f;
    float scaleTension_ = 0.0f;
};

}  // namespace geode::sound
