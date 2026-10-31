#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

// The TouchDesigner half of the engine. TouchDesigner analyses audio time-sliced: each band is a filter
// running over every sample (its Audio Filter CHOP, 4-pole), an envelope follows the filtered power
// (Envelope CHOP), and once per frame the slice of samples since the last frame is reduced to one number
// (Analyze CHOP), which then goes through Lag and Trigger. The filters see every sample as it is written,
// so a hit lands on the frame it happened in, with none of a long FFT window's smear.
namespace geode::sound {

// One RBJ-cookbook Butterworth biquad, transposed direct form II.
class Biquad {
public:
    enum Kind { kLowPass, kHighPass };

    void set(Kind kind, float hz, int sampleRateHz) {
        const double w = 2.0 * std::numbers::pi * std::min<double>(hz, 0.45 * sampleRateHz) / sampleRateHz;
        const double cw = std::cos(w);
        const double alpha = std::sin(w) / (2.0 * 0.70710678);
        const double a0 = 1.0 + alpha;
        const double edge = kind == kLowPass ? 1.0 - cw : 1.0 + cw;
        b0_ = static_cast<float>(0.5 * edge / a0);
        b1_ = static_cast<float>((kind == kLowPass ? edge : -edge) / a0);
        b2_ = b0_;
        a1_ = static_cast<float>(-2.0 * cw / a0);
        a2_ = static_cast<float>((1.0 - alpha) / a0);
    }
    float process(float x) {
        const float y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        if (std::fabs(z1_) < 1e-20f) z1_ = 0.0f;  // silence must not decay into denormals
        if (std::fabs(z2_) < 1e-20f) z2_ = 0.0f;
        return y;
    }
    void clear() { z1_ = z2_ = 0.0f; }

private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};

// One band of the bank: an optional 4-pole high-pass and 4-pole low-pass (two Butterworth biquads each,
// 24 dB/octave), a power envelope with a 15 ms time constant, and the slice's peak of that envelope.
// The envelope is what keeps a 120 fps slice (8 ms, shorter than one cycle of 50 Hz) from rippling.
//
// It also measures punch at audio rate, for the drum triggers: the level over its own running mean
// (~4 s, MilkDrop's 1.0 = typical), minus that ratio's own recent past (150 ms). The slice keeps the
// highest punch it saw, so "did this band jump past the threshold" has the same answer at 30 fps as at
// 120: a frame-rate reduction of a per-frame ratio would not (a longer slice's peak lifts its own mean).
class SliceBand {
public:
    SliceBand() = default;
    void set(float loHz, float hiHz, int sampleRateHz) {
        hasLow_ = loHz > 0.0f;
        hasHigh_ = hiHz > 0.0f && hiHz < 0.45f * static_cast<float>(sampleRateHz);
        for (auto& f : hp_) f.set(Biquad::kHighPass, std::max(loHz, 1.0f), sampleRateHz);
        for (auto& f : lp_) f.set(Biquad::kLowPass, std::max(hiHz, 1.0f), sampleRateHz);
        const float fs = static_cast<float>(sampleRateHz);
        follow_ = 1.0f - std::exp(-1.0f / (0.015f * fs));
        meanWarm_ = 1.0f - std::exp(-1.0f / (0.32f * fs));
        meanSteady_ = 1.0f - std::exp(-1.0f / (4.15f * fs));
        refFollow_ = 1.0f - std::exp(-1.0f / (0.15f * fs));
        warmSamples_ = static_cast<int>(50.0f / 30.0f * fs);
    }
    void process(float x) {
        if (hasLow_) x = hp_[1].process(hp_[0].process(x));
        if (hasHigh_) x = lp_[1].process(lp_[0].process(x));
        power_ += follow_ * (x * x - power_);
        if (power_ < 1e-20f) power_ = 0.0f;
        peak_ = std::max(peak_, power_);
        // Digital silence freezes the mean, so the first hit after a pause is measured against the music.
        if (power_ < 1e-12f) return;
        const float level = std::sqrt(power_);
        if (heard_ == 0) mean_ = level;
        mean_ += (heard_ < warmSamples_ ? meanWarm_ : meanSteady_) * (level - mean_);
        if (heard_ < warmSamples_) heard_++;
        const float ratio = std::min(level / mean_, 4.0f);
        punch_ = std::max(punch_, ratio - ref_);
        ref_ += refFollow_ * (ratio - ref_);
    }
    // The slice's level (RMS-like, linear), then starts the next slice from where the envelope is now.
    float take() {
        const float level = std::sqrt(peak_);
        peak_ = power_;
        return level;
    }
    // The slice's highest punch (0 when it held none), then starts the next slice.
    float takePunch() {
        const float p = punch_;
        punch_ = 0.0f;
        return p;
    }
    void clear() { *this = SliceBand(*this, 0); }

private:
    // A copy with the filter design kept and every running value back at rest.
    SliceBand(const SliceBand& design, int)
        : hasLow_(design.hasLow_), hasHigh_(design.hasHigh_), follow_(design.follow_),
          meanWarm_(design.meanWarm_), meanSteady_(design.meanSteady_), refFollow_(design.refFollow_),
          warmSamples_(design.warmSamples_) {
        hp_ = design.hp_;
        lp_ = design.lp_;
        for (auto& f : hp_) f.clear();
        for (auto& f : lp_) f.clear();
    }

    std::array<Biquad, 2> hp_;
    std::array<Biquad, 2> lp_;
    bool hasLow_ = false;
    bool hasHigh_ = false;
    float follow_ = 0.0f;
    float meanWarm_ = 0.0f;
    float meanSteady_ = 0.0f;
    float refFollow_ = 0.0f;
    int warmSamples_ = 0;
    float power_ = 0.0f;
    float peak_ = 0.0f;
    float mean_ = 0.0f;
    float ref_ = 1.0f;
    float punch_ = 0.0f;
    int heard_ = 0;
};

}  // namespace geode::sound
