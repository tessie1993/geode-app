#include "audio/dsp/Loudness.hpp"

#include <algorithm>
#include <cmath>

// Same numbers as the Kotlin this replaced: the JVM never fuses a multiply and an add.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace geode::audio {
namespace {

constexpr double kPi = 3.141592653589793;

// The -0.691 dB offset that puts a 1 kHz sine at -3.01 dBFS on 0 LKFS.
constexpr double kLoudnessOffset = -0.691;
constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kRelativeGateLu = 10.0;
// +1.5 dB on the surrounds, i.e. 10^(1.5/20).
constexpr double kSurroundWeight = 1.41;
constexpr double kSilenceDb = -120.0;
constexpr double kNearZero = 1e-12;

// K-weighting constants (the analog prototypes of the BS.1770 filters).
constexpr double kShelfHz = 1681.974450955533;
constexpr double kShelfGainDb = 3.999843853973347;
constexpr double kShelfQ = 0.7071752369554196;
constexpr double kShelfBandExponent = 0.4996667741545416;
constexpr double kHighPassHz = 38.13547087602444;
constexpr double kHighPassQ = 0.5003270373238773;

double loudnessOf(double power) { return kLoudnessOffset + 10.0 * std::log10(power); }

double decibels(double linear) { return linear <= 0.0 ? kSilenceDb : 20.0 * std::log10(linear); }

}  // namespace

double LoudnessAnalyser::Biquad::step(double* s, double x) const {
    const double y = b0 * x + b1 * s[0] + b2 * s[1] - a1 * s[2] - a2 * s[3];
    s[1] = s[0];
    s[0] = x;
    s[3] = s[2];
    s[2] = y;
    return y;
}

LoudnessAnalyser::LoudnessAnalyser(int sampleRate, int channels)
    : sampleRate_(sampleRate), channels_(channels) {
    // Android interleaves in channel-mask order (FL, FR, FC, LFE, BL, BR), exactly the order BS.1770
    // weights. The LFE is excluded outright, as the spec requires.
    if (channels == 6) {
        weights_ = {1.0, 1.0, 1.0, 0.0, kSurroundWeight, kSurroundWeight};
    } else {
        weights_.assign(static_cast<size_t>(channels), 1.0);
    }
    // 100 ms of samples, rounded half up like Kotlin's roundToInt. Exact at 44.1 kHz, within one sample
    // elsewhere: far below the resolution of a gated mean.
    stepSamples_ = std::max(1, static_cast<int>(std::floor(sampleRate / 10.0 + 0.5)));
    blockSamples_ = stepSamples_ * kStepsPerBlock;

    // Stage 1: a +4 dB high shelf at ~1681 Hz (the head's acoustic effect), by bilinear transform so it
    // is right at 44.1 kHz too, not only at the 48 kHz the spec tabulates.
    {
        const double k = std::tan(kPi * kShelfHz / sampleRate);
        const double vh = std::pow(10.0, kShelfGainDb / 20.0);
        const double vb = std::pow(vh, kShelfBandExponent);
        const double kk = k * k;
        const double a0 = 1.0 + k / kShelfQ + kk;
        shelf_ = {(vh + vb * k / kShelfQ + kk) / a0, 2.0 * (kk - vh) / a0, (vh - vb * k / kShelfQ + kk) / a0,
                  2.0 * (kk - 1.0) / a0, (1.0 - k / kShelfQ + kk) / a0};
    }
    // Stage 2: the RLB high-pass at ~38 Hz that discards inaudible rumble.
    {
        const double k = std::tan(kPi * kHighPassHz / sampleRate);
        const double kk = k * k;
        const double a0 = 1.0 + k / kHighPassQ + kk;
        highPass_ = {1.0, -2.0, 1.0, 2.0 * (kk - 1.0) / a0, (1.0 - k / kHighPassQ + kk) / a0};
    }
    const auto n = static_cast<size_t>(channels);
    shelfState_.assign(n * 4, 0.0);
    highPassState_.assign(n * 4, 0.0);
    stepSumSquares_.assign(n, 0.0);
    stepRing_.assign(n * kStepsPerBlock, 0.0);
    blockLoudness_.reserve(1024);
    blockPower_.reserve(1024);

    // BS.1770-4 Annex 2 prints a 48-tap table; the phase filters are generated from the design it
    // describes instead (a sinc low-pass at fs/4, 49 taps, Hann windowed, split into four polyphase
    // branches, the libebur128 construction). The spec's 12.04 dB pre-attenuation only guards fixed-point
    // overflow, so in double precision it is omitted.
    for (int tap = 0; tap < kTaps; ++tap) {
        const double centred = tap - (kTaps - 1) / 2.0;
        const double x = centred * kPi / kFactor;
        const double sinc = std::fabs(x) < kNearZero ? 1.0 : std::sin(x) / x;
        const double hann = 0.5 * (1.0 - std::cos(2.0 * kPi * tap / (kTaps - 1)));
        phases_[tap % kFactor][tap / kFactor] = sinc * hann;
    }
    history_.assign(n * kDelay * 2, 0.0);
}

void LoudnessAnalyser::feed(const float* interleaved, size_t count) {
    const size_t available = count / static_cast<size_t>(channels_);
    const float* frame = interleaved;
    for (size_t f = 0; f < available; ++f, frame += channels_) {
        for (int c = 0; c < channels_; ++c) {
            const double x = frame[c];
            samplePeak_ = std::max(samplePeak_, std::fabs(x));
            const double weighted = highPass_.step(&highPassState_[c * 4], shelf_.step(&shelfState_[c * 4], x));
            stepSumSquares_[c] += weighted * weighted;
        }
        pushTruePeak(frame);
        if (++stepFilled_ == stepSamples_) closeStep();
    }
    frames_ += static_cast<int64_t>(available);
}

void LoudnessAnalyser::pushTruePeak(const float* frame) {
    const int slot = cursor_;
    for (int c = 0; c < channels_; ++c) {
        double* h = &history_[static_cast<size_t>(c) * kDelay * 2];
        const double x = frame[c];
        h[slot] = x;
        h[slot + kDelay] = x;
        const double* newest = h + slot + kDelay;
        for (const auto& phase : phases_) {
            double acc = 0.0;
            for (int tap = 0; tap < kDelay; ++tap) acc += phase[tap] * newest[-tap];
            truePeak_ = std::max(truePeak_, std::fabs(acc));
        }
    }
    cursor_ = slot + 1 == kDelay ? 0 : slot + 1;
}

void LoudnessAnalyser::closeStep() {
    for (int c = 0; c < channels_; ++c) {
        stepRing_[c * kStepsPerBlock + ringSlot_] = stepSumSquares_[c];
        stepSumSquares_[c] = 0.0;
    }
    ringSlot_ = ringSlot_ + 1 == kStepsPerBlock ? 0 : ringSlot_ + 1;
    stepFilled_ = 0;
    // A block exists once four consecutive 100 ms steps are in the ring; the tail of the programme that
    // cannot fill one is dropped, as the spec requires.
    if (++stepsClosed_ >= kStepsPerBlock) closeBlock();
}

void LoudnessAnalyser::closeBlock() {
    double power = 0.0;
    for (int c = 0; c < channels_; ++c) {
        const double weight = weights_[c];
        if (weight == 0.0) continue;
        double sum = 0.0;
        for (int slot = 0; slot < kStepsPerBlock; ++slot) sum += stepRing_[c * kStepsPerBlock + slot];
        power += weight * (sum / blockSamples_);
    }
    // Digital silence is minus infinity, which no gate can admit; nothing to record.
    if (power <= 0.0) return;
    blockLoudness_.push_back(loudnessOf(power));
    blockPower_.push_back(power);
}

LoudnessReport LoudnessAnalyser::finish() const {
    LoudnessReport report;
    // The oversampler reconstructs between samples, but a stored sample is itself a peak the
    // reconstruction must clear, so the honest answer is whichever is larger.
    report.truePeakDbtp = decibels(std::max(truePeak_, samplePeak_));
    report.samplePeakDbfs = decibels(samplePeak_);
    report.measuredMs = frames_ * 1000 / sampleRate_;

    double absoluteSum = 0.0;
    int absoluteCount = 0;
    for (size_t i = 0; i < blockLoudness_.size(); ++i) {
        if (blockLoudness_[i] > kAbsoluteGateLufs) {
            absoluteSum += blockPower_[i];
            ++absoluteCount;
        }
    }
    if (absoluteCount == 0) return report;
    // The relative gate is set from the mean power of everything that cleared -70 LUFS, then dropped
    // 10 LU; that is what stops long fades and room tone from dragging the answer down.
    const double relativeGate = loudnessOf(absoluteSum / absoluteCount) - kRelativeGateLu;
    double sum = 0.0;
    int count = 0;
    for (size_t i = 0; i < blockLoudness_.size(); ++i) {
        const double loudness = blockLoudness_[i];
        if (loudness > kAbsoluteGateLufs && loudness > relativeGate) {
            sum += blockPower_[i];
            ++count;
        }
    }
    if (count == 0) return report;
    report.hasIntegrated = true;
    report.integratedLufs = loudnessOf(sum / count);
    report.gatedBlockCount = count;
    return report;
}

}  // namespace geode::audio
