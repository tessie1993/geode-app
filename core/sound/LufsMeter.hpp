#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// The sound engine's absolute loudness, as fosfora measures it (crates/fosfora-app/src/audio/loudness.rs):
// ITU-R BS.1770 K-weighting on each channel at the engine's own rate, left² + right² summed, momentary (400 ms)
// and short-term (3 s) windows as running sums. Nothing here is relative to the song: -60 LUFS reads 0 and
// 0 LUFS reads 1 on every track, so an ambient pad and a mastered kick read as the different loudnesses they
// are. The K-weighting constants are the export meter's (core/audio/dsp/Loudness.cpp).
//
// The windows are summed per analysis hop (the engine's fixed 10 ms), not per sample: 40 hops are exactly
// 400 ms, 300 are 3 s, and the numbers depend on the samples alone, never on the frame rate.
//
// The one silence gate (fosfora R8) lives here too, so every detector reads one flag: it opens when the
// momentary loudness reaches -55 LUFS, closes after 250 ms under -60 LUFS, and starts closed.
namespace geode::sound {

class LufsMeter {
public:
    static constexpr double kFloorLufs = -60.0;     // maps to 0
    static constexpr double kGateOpenLufs = -55.0;
    static constexpr double kGateCloseLufs = -60.0;
    static constexpr double kGateCloseSeconds = 0.25;
    static constexpr double kMomentarySeconds = 0.4;
    static constexpr double kShortTermSeconds = 3.0;
    static constexpr double kMaxLu = 8.0;           // the trend's full scale: M above S by this much reads 1

    void configure(int sampleRateHz, int hopSamples) {
        rate_ = std::max(8000, sampleRateHz);
        hopSamples_ = std::max(1, hopSamples);
        hopSeconds_ = static_cast<double>(hopSamples_) / rate_;
        const int m = std::max(1, static_cast<int>(std::lround(kMomentarySeconds / hopSeconds_)));
        const int s = std::max(m, static_cast<int>(std::lround(kShortTermSeconds / hopSeconds_)));
        ring_.assign(static_cast<size_t>(s), 0.0);
        momentaryHops_ = m;
        closeHops_ = std::max(1, static_cast<int>(std::lround(kGateCloseSeconds / hopSeconds_)));
        shelf_ = shelf(rate_);
        highPass_ = highPass(rate_);
        reset();
    }

    void reset() {
        std::fill(ring_.begin(), ring_.end(), 0.0);
        head_ = 0;
        filled_ = 0;
        sumM_ = 0.0;
        sumS_ = 0.0;
        hopSum_ = 0.0;
        for (auto& c : state_) c = ChannelState{};
        momentary_ = kSilence;
        shortTerm_ = kSilence;
        open_ = false;
        belowHops_ = 0;
    }

    // One stereo sample, in -1..1 (or whatever the trim made of it).
    void process(float l, float r) {
        const double wl = weigh(state_[0], l);
        const double wr = weigh(state_[1], r);
        hopSum_ += wl * wl + wr * wr;
    }

    // Closes the hop: its mean square joins both windows, the gate steps.
    void hop() {
        const double ms = hopSum_ / hopSamples_;
        hopSum_ = 0.0;
        const size_t n = ring_.size();
        const size_t tail = (head_ + n - static_cast<size_t>(momentaryHops_)) % n;  // leaves the momentary window
        if (filled_ >= momentaryHops_) sumM_ -= ring_[tail];
        if (filled_ >= static_cast<int>(n)) sumS_ -= ring_[head_];  // the oldest leaves the short-term window
        ring_[head_] = ms;
        sumM_ += ms;
        sumS_ += ms;
        head_ = (head_ + 1) % n;
        filled_ = std::min(filled_ + 1, static_cast<int>(n));
        // Windows still filling read over the hops they have, so a track's first notes already measure.
        momentary_ = lufs(std::max(0.0, sumM_) / std::min(filled_, momentaryHops_));
        shortTerm_ = lufs(std::max(0.0, sumS_) / filled_);
        if (!open_) {
            if (momentary_ >= kGateOpenLufs) {
                open_ = true;
                belowHops_ = 0;
            }
        } else {
            belowHops_ = momentary_ < kGateCloseLufs ? belowHops_ + 1 : 0;
            if (belowHops_ >= closeHops_) open_ = false;
        }
    }

    double momentaryLufs() const { return momentary_; }
    double shortTermLufs() const { return shortTerm_; }
    // -60..0 LUFS onto 0..1, absolute.
    static float unit(double lufs) { return static_cast<float>(std::clamp((lufs - kFloorLufs) / -kFloorLufs, 0.0, 1.0)); }
    float momentary() const { return unit(momentary_); }
    float shortTerm() const { return unit(shortTerm_); }
    // fosfora's trend: how far the momentary loudness is above the short-term one, 0..8 LU onto 0..1.
    float trend() const { return static_cast<float>(std::clamp(momentary_ - shortTerm_, 0.0, kMaxLu) / kMaxLu); }
    bool open() const { return open_; }

private:
    static constexpr double kSilence = -120.0;
    static constexpr double kPi = 3.141592653589793;
    static constexpr double kOffset = -0.691;

    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    struct ChannelState {
        double s[4]{};  // shelf: x1 x2 y1 y2
        double h[4]{};  // high-pass
    };

    static double lufs(double ms) { return ms > 1e-12 ? kOffset + 10.0 * std::log10(ms) : kSilence; }

    static double run(const Biquad& q, double* s, double x) {
        const double y = q.b0 * x + q.b1 * s[0] + q.b2 * s[1] - q.a1 * s[2] - q.a2 * s[3];
        s[1] = s[0];
        s[0] = x;
        s[3] = s[2];
        s[2] = y;
        return y;
    }
    double weigh(ChannelState& c, float x) const { return run(highPass_, c.h, run(shelf_, c.s, static_cast<double>(x))); }

    // BS.1770's two stages by bilinear transform, right at any rate (the constants of Loudness.cpp).
    static Biquad shelf(int rate) {
        const double k = std::tan(kPi * 1681.974450955533 / rate);
        const double vh = std::pow(10.0, 3.999843853973347 / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double q = 0.7071752369554196;
        const double kk = k * k;
        const double a0 = 1.0 + k / q + kk;
        return {(vh + vb * k / q + kk) / a0, 2.0 * (kk - vh) / a0, (vh - vb * k / q + kk) / a0, 2.0 * (kk - 1.0) / a0,
                (1.0 - k / q + kk) / a0};
    }
    static Biquad highPass(int rate) {
        const double k = std::tan(kPi * 38.13547087602444 / rate);
        const double q = 0.5003270373238773;
        const double kk = k * k;
        const double a0 = 1.0 + k / q + kk;
        return {1.0, -2.0, 1.0, 2.0 * (kk - 1.0) / a0, (1.0 - k / q + kk) / a0};
    }

    int rate_ = 48000;
    int hopSamples_ = 480;
    double hopSeconds_ = 0.01;
    int momentaryHops_ = 40;
    int closeHops_ = 25;
    Biquad shelf_{};
    Biquad highPass_{};
    ChannelState state_[2]{};
    std::vector<double> ring_;
    size_t head_ = 0;
    int filled_ = 0;
    double sumM_ = 0.0;
    double sumS_ = 0.0;
    double hopSum_ = 0.0;
    double momentary_ = kSilence;
    double shortTerm_ = kSilence;
    bool open_ = false;
    int belowHops_ = 0;
};

}  // namespace geode::sound
