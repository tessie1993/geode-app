#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "sound/Fft.hpp"

namespace geode::sound {

// Kick drum events from a trained model: fosfora's kick detector (tessie1993/fosforatess, kick_model.rs,
// board #3624), ported line for line so it scores what fosfora's does.
//
// The rhythm's onset envelope hears every rise in the low bands, and a bass note on the "and" rises there
// as much as a kick on the beat; no onset function tells them apart. The model does: 300 boosted trees over
// the spectral shape of the attack (flux and level in eleven bands from 30 Hz to 10 kHz, the low span's
// flatness and peakiness) for this hop and the two before it, trained on drum stems.
//
//   mono ─► DC blocker (5 Hz) ─► halved while the rate stays ≥ 44.1 kHz ─► 512-sample hops
//      ─► Hann 4096 (≤ 250 Hz) and Hann 1024 (above) ─► 24 features × 3 hops ─► trees ─► probability
//      ─► kick = probability mapped so the trained threshold reads 0.5 ─► event: armed below 0.3,
//         fires at 0.5, at most one per 100 ms
//
// Everything is sized in configure(); push() never allocates.
class KickModel {
public:
    static constexpr int kHop = 512;
    static constexpr int kLarge = 4096;
    static constexpr int kMedium = 1024;
    static constexpr int kBands = 11;
    static constexpr int kFrame = 2 * kBands + 2;
    static constexpr int kContext = 3;
    static constexpr int kInputs = kFrame * kContext;

    void configure(int sampleRateHz);
    // One mono sample at the source rate. True when a kick event fires on it.
    bool push(float mono);
    void reset();

    // The last hop's model probability, and the kick envelope (0.5 = the trained threshold).
    float probability() const { return probability_; }
    float kick() const { return kick_; }
    // Seconds of analysis audio heard, at the analysis rate: the clock events are stamped on.
    double seconds() const { return static_cast<double>(analysed_) / analysisRate_; }

    // The ensemble alone, for tests: the probability of one input vector.
    static float score(const std::array<float, kInputs>& x);
    // The trained threshold's mapping onto the kick envelope.
    static float kickFromProbability(float p);

private:
    struct Band {
        bool large;
        int lo;
        int hi;
    };
    struct Halfband {
        std::array<float, 64> hist{};  // the newest taps, as a ring
        int head = 0;
        bool emit = true;
    };
    bool analyse(float x);
    void hop();
    void features(std::array<float, kFrame>& f) const;

    int analysisRate_ = 48000;
    int stages_ = 0;
    std::array<Halfband, 3> halfbands_{};  // up to 352.8 kHz
    float dcR_ = 0.0f;
    float dcX_ = 0.0f;
    float dcY_ = 0.0f;

    std::array<Band, kBands> bands_{};
    int lowLo_ = 0;
    int lowHi_ = 0;
    std::vector<float> ring_;  // the newest kLarge analysis samples, as a ring
    int head_ = 0;
    int fill_ = 0;             // samples since the last hop
    int64_t analysed_ = 0;
    Fft large_{kLarge};
    Fft medium_{kMedium};
    std::vector<float> largeWin_, mediumWin_, frame_, windowed_;
    float largeScale_ = 0.0f;
    float mediumScale_ = 0.0f;
    std::vector<float> largeMag_, largePrev_, mediumMag_, mediumPrev_;
    std::array<std::array<float, kFrame>, kContext - 1> history_{};

    float probability_ = 0.0f;
    float kick_ = 0.0f;
    bool armed_ = false;
    bool fired_ = false;
    double lastKick_ = -1e9;
};

}  // namespace geode::sound
