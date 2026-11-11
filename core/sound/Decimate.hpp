#pragma once
#include <array>
#include <cstddef>
#include <cmath>
#include <numbers>

namespace geode::sound {

// Halves a sample rate: a 47-tap half-band low-pass (windowed sinc, Blackman), then every second sample.
// The sound engine runs it once per halving on rates of 88.2 kHz and up, so 88.2/96 kHz and 176.4/192 kHz
// are heard at 44.1/48 kHz: the same window, filter bank and hop as the common rates, at a half or a
// quarter of the cost, instead of an FFT twice or four times as long over content no detector reads
// (fosfora's audio/decimate.rs). Flat to within 0.1 dB up to 0.2 of the input rate (19.2 kHz at 96 kHz),
// 50 dB down at 0.3 (28.8 kHz) and 79 dB from 0.32, so what folds back lands above 19 kHz. Delay: 23
// input samples.
class HalfBand {
public:
    static constexpr int kTaps = 47;

    HalfBand() {
        // Every second tap of a half-band filter is zero, and the rest are symmetric: the centre, plus one
        // weight per pair at odd offsets 1, 3 … 23 from it.
        const auto tap = [](int i) {
            const double x = i - kHalf;
            const double sinc = x == 0.0 ? 0.5 : std::sin(0.5 * std::numbers::pi * x) / (std::numbers::pi * x);
            const double w = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (kTaps - 1)) +
                             0.08 * std::cos(4.0 * std::numbers::pi * i / (kTaps - 1));
            return sinc * w;
        };
        double sum = tap(kHalf);
        for (size_t m = 0; m < pair_.size(); m++) sum += 2.0 * tap(kHalf + 2 * static_cast<int>(m) + 1);
        centre_ = static_cast<float>(tap(kHalf) / sum);  // unity at DC
        for (size_t m = 0; m < pair_.size(); m++) pair_[m] = static_cast<float>(tap(kHalf + 2 * static_cast<int>(m) + 1) / sum);
    }

    // One input sample; true when it completes an output sample, written to `out`.
    bool push(float x, float& out) {
        // Written twice, so the last kTaps samples always sit in one run from history_[head_].
        history_[static_cast<size_t>(head_)] = x;
        history_[static_cast<size_t>(head_ + kTaps)] = x;
        head_ = (head_ + 1) % kTaps;
        odd_ = !odd_;
        if (odd_) return false;
        const float* w = history_.data() + head_ + kHalf;
        float acc = centre_ * w[0];
        for (size_t m = 0; m < pair_.size(); m++) {
            const int d = 2 * static_cast<int>(m) + 1;
            acc += pair_[m] * (w[-d] + w[d]);
        }
        out = acc;
        return true;
    }

    void reset() {
        history_ = {};
        head_ = 0;
        odd_ = false;
    }

private:
    static constexpr int kHalf = kTaps / 2;
    float centre_ = 0.5f;
    std::array<float, (kHalf + 1) / 2> pair_{};
    std::array<float, 2 * kTaps> history_{};
    int head_ = 0;
    bool odd_ = false;
};

}  // namespace geode::sound
