#include "audio/dsp/PcmResampler.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace geode::audio {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Longest half filter in input frames; only absurd ratios (far below 8 kHz out from 192 kHz) reach it.
constexpr int kMaxHalf = 4096;
// Coefficients a table may hold (8 MB of float) before it rounds positions to fewer rows.
constexpr int64_t kMaxCoefficients = int64_t{1} << 21;

// Zeroth-order modified Bessel function of the first kind, by its power series (the Kaiser window).
double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 64; ++k) {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

int16_t toPcm16(float v) {
    return static_cast<int16_t>(std::lrint(std::clamp(v, -32768.0f, 32767.0f)));
}

}  // namespace

PcmResampler::PcmResampler(int inRateHz, int outRateHz, int channels)
    : inRate_(inRateHz), outRate_(outRateHz), channels_(channels) {
    if (inRate_ == outRate_ || inRate_ <= 0 || outRate_ <= 0 || channels_ <= 0) return;

    // Cutoff in cycles per input frame: kRolloff of the lower of the two Nyquist rates.
    const double scale = std::min(1.0, static_cast<double>(outRate_) / static_cast<double>(inRate_));
    const double fc = 0.5 * kRolloff * scale;
    // sinc(2 fc x) crosses zero every 1 / (2 fc) input frames; keep kZeroCrossings of them on each side.
    half_ = std::clamp(static_cast<int>(std::ceil(kZeroCrossings / (2.0 * fc))), 1, kMaxHalf);
    const int taps = 2 * half_;

    // Output k sits at fraction ((k * in) % out) / out past an input frame: out / gcd distinct values.
    const int64_t g = std::gcd(static_cast<int64_t>(inRate_), static_cast<int64_t>(outRate_));
    const int64_t exactPhases = outRate_ / g;
    const int64_t cap = std::clamp<int64_t>(kMaxCoefficients / taps, 256, kMaxPhases);
    exact_ = exactPhases <= cap;
    phases_ = exact_ ? exactPhases : cap;
    phaseDiv_ = exact_ ? g : 1;

    table_.assign(static_cast<size_t>((phases_ + 1) * taps), 0.0f);
    const double i0Beta = besselI0(kKaiserBeta);
    std::vector<double> row(static_cast<size_t>(taps));
    for (int64_t p = 0; p <= phases_; ++p) {
        const double frac = static_cast<double>(p) / static_cast<double>(phases_);
        double sum = 0.0;
        for (int m = 0; m < taps; ++m) {
            // For an output at position i0 + frac, tap m reads input frame i0 - half_ + 1 + m.
            const double x = static_cast<double>(m - half_ + 1) - frac;
            const double r = x / static_cast<double>(half_);
            const double window = std::fabs(r) >= 1.0 ? 0.0 : besselI0(kKaiserBeta * std::sqrt(1.0 - r * r)) / i0Beta;
            const double arg = 2.0 * fc * x;
            const double sinc = std::fabs(arg) < 1e-12 ? 1.0 : std::sin(kPi * arg) / (kPi * arg);
            const double h = 2.0 * fc * sinc * window;
            row[static_cast<size_t>(m)] = h;
            sum += h;
        }
        // Unity gain at DC on every row, so a constant passes untouched whatever the phase.
        for (int m = 0; m < taps; ++m) {
            table_[static_cast<size_t>(p * taps + m)] = static_cast<float>(row[static_cast<size_t>(m)] / sum);
        }
    }
    // The silence before the stream: 2 * half_ frames of zero history.
    work_.assign(static_cast<size_t>(taps) * static_cast<size_t>(channels_), 0.0f);
}

size_t PcmResampler::maxOutFrames(size_t frames) const {
    if (inRate_ == outRate_) return frames;
    // A call writes the outputs k whose newest input, floor(k * in / out), falls among its frames: at
    // most ceil(frames * out / in) of them.
    return static_cast<size_t>(static_cast<int64_t>(frames) * outRate_ / inRate_ + 2);
}

void PcmResampler::reserve(size_t frames) {
    if (inRate_ == outRate_ || half_ <= 0) return;
    work_.reserve((static_cast<size_t>(2 * half_) + frames) * static_cast<size_t>(channels_));
}

size_t PcmResampler::process(const int16_t* in, size_t frames, int16_t* out) {
    if (inRate_ == outRate_) {
        std::copy(in, in + frames * static_cast<size_t>(channels_), out);
        return frames;
    }
    if (frames == 0) return 0;
    const size_t ch = static_cast<size_t>(channels_);
    const int taps = 2 * half_;
    const size_t history = static_cast<size_t>(taps) * ch;

    // work_ = [the last 2 * half_ input frames][this call's frames]: work frame w is absolute input
    // frame inFrames_ - 2 * half_ + w.
    work_.resize(history + frames * ch);
    for (size_t i = 0; i < frames * ch; ++i) work_[history + i] = static_cast<float>(in[i]);
    const int64_t end = inFrames_ + static_cast<int64_t>(frames);
    const int64_t workStart = inFrames_ - taps;

    size_t written = 0;
    for (;;) {
        const int64_t num = outFrames_ * inRate_;
        // Output k is centred on newest - half_ + frac and reads input frames newest - 2 * half_ + 1 ..
        // newest. Every earlier output was written by an earlier call, so newest >= inFrames_ here and
        // the oldest frame read is inside work_.
        const int64_t newest = num / outRate_;
        if (newest >= end) break;
        const int64_t rem = num % outRate_;
        // A rounded position may land on row phases_ (fraction 1): its taps are laid out on the same
        // frames, with the oldest one weightless, so no frame past newest is needed.
        const int64_t phase = exact_ ? rem / phaseDiv_ : (rem * phases_ + outRate_ / 2) / outRate_;
        const float* h = table_.data() + static_cast<size_t>(phase * taps);
        const float* x = work_.data() + static_cast<size_t>(newest - taps + 1 - workStart) * ch;
        for (size_t c = 0; c < ch; ++c) {
            float acc = 0.0f;
            const float* xc = x + c;
            for (int m = 0; m < taps; ++m) acc += h[m] * xc[static_cast<size_t>(m) * ch];
            out[written++] = toPcm16(acc);
        }
        ++outFrames_;
    }

    // Keep the newest 2 * half_ frames as the next call's history.
    std::copy(work_.end() - static_cast<std::ptrdiff_t>(history), work_.end(), work_.begin());
    work_.resize(history);
    inFrames_ = end;
    return written / ch;
}

}  // namespace geode::audio
