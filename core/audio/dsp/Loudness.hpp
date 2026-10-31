#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace geode::audio {

// What ITU-R BS.1770-4 says about one finished programme.
struct LoudnessReport {
    // False when every 400 ms block sits under the -70 LUFS absolute gate: the spec's answer is minus
    // infinity, a different fact from "it measured -70", so it is a flag rather than a sentinel number.
    bool hasIntegrated = false;
    double integratedLufs = 0.0;
    // Inter-sample peak from the 4x oversampled signal (never under the sample peak).
    double truePeakDbtp = 0.0;
    double samplePeakDbfs = 0.0;
    int64_t measuredMs = 0;
    int gatedBlockCount = 0;
};

// ITU-R BS.1770-4 integrated loudness and true peak, fed interleaved float samples in -1..1. Used on
// exported files (export/LoudnessMeter decodes them and feeds this). Ported unchanged from the Kotlin
// LoudnessAnalyser, double precision throughout and FP contraction off, so numbers did not move.
//
// The chain per BS.1770:
//  1. K-weighting: a high-shelf pre-filter then a high-pass, both derived for the stream's own sample
//     rate rather than hard-coded at 48 kHz.
//  2. 400 ms blocks with 75% overlap (a new block every 100 ms).
//  3. Absolute gate at -70 LUFS, then a relative gate 10 LU under the mean of what survived.
// True peak is measured on the unweighted signal through a 4x oversampler, per Annex 2.
class LoudnessAnalyser {
public:
    // BS.1770 defines channel weights for mono, stereo and 5.1 only.
    static bool supports(int channels) { return channels == 1 || channels == 2 || channels == 6; }

    // `channels` must pass supports() and `sampleRate` be positive.
    LoudnessAnalyser(int sampleRate, int channels);

    // Consumes `count` interleaved samples (not frames); a trailing partial frame is ignored.
    void feed(const float* interleaved, size_t count);

    // True once at least one full 400 ms block exists, i.e. once there is anything to gate.
    bool hasCompleteBlock() const { return !blockLoudness_.empty(); }

    // Non-destructive: feeding may continue afterwards.
    LoudnessReport finish() const;

private:
    struct Biquad {
        double b0, b1, b2, a1, a2;
        // Direct form I; `s` is x[n-1], x[n-2], y[n-1], y[n-2].
        double step(double* s, double x) const;
    };

    static constexpr int kStepsPerBlock = 4;
    static constexpr int kTaps = 49;
    static constexpr int kFactor = 4;
    static constexpr int kDelay = (kTaps + kFactor - 1) / kFactor;

    void closeStep();
    void closeBlock();
    void pushTruePeak(const float* frame);

    int sampleRate_;
    int channels_;
    std::vector<double> weights_;
    int stepSamples_;
    int blockSamples_;
    Biquad shelf_;
    Biquad highPass_;
    std::vector<double> shelfState_;
    std::vector<double> highPassState_;
    std::vector<double> stepSumSquares_;
    int stepFilled_ = 0;
    // The last four 100 ms sums per channel; a 400 ms block is their total.
    std::vector<double> stepRing_;
    int ringSlot_ = 0;
    int stepsClosed_ = 0;
    std::vector<double> blockLoudness_;
    std::vector<double> blockPower_;
    // True-peak oversampler: four polyphase branches; each sample is stored twice, kDelay apart, so the
    // tap loop walks straight back from the cursor without a wrap test per tap.
    std::array<std::array<double, kDelay>, kFactor> phases_{};
    std::vector<double> history_;
    int cursor_ = 0;
    double truePeak_ = 0.0;
    double samplePeak_ = 0.0;
    int64_t frames_ = 0;
};

}  // namespace geode::audio
