#include "sound/interp/Waveform.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "sound/SoundEngine.hpp"

namespace geode::sound::interp {

namespace {
// The engine's DC blocker corner.
constexpr float kDcCornerHz = 10.0f;
}  // namespace

Waveform::Waveform(AnalysisSettings settings) : fftSize_(settings.checked().fftSize) {
    // Only the middle of the drawn wave counts when aligning, so the centre is what holds still.
    const float c = kPoints;
    for (int i = 0; i < kPoints; i++) {
        const float x = static_cast<float>(i);
        const float tri = i < kPoints / 2 ? 2.0f * x / c : 2.0f * (c - 1.0f - x) / c;
        weight_[static_cast<size_t>(i)] = std::clamp(5.0f * tri - 3.2f, 0.0f, 1.0f);
    }
}

void Waveform::setTrimDb(float db) { trim_ = std::isfinite(db) ? std::pow(10.0f, db / 20.0f) : 1.0f; }

void Waveform::reset() {
    mono_.fill(0.0f);
    head_ = 0;
    dcX_ = dcY_ = 0.0f;
    hasPrev_ = false;
}

void Waveform::write(const float* interleaved, int frames, int channels, int sampleRateHz) {
    if (interleaved == nullptr || frames <= 0 || channels <= 0 || sampleRateHz <= 0) return;
    if (sampleRateHz != rateHz_) {
        rateHz_ = sampleRateHz;
        // The engine's block (its configured window / 512 analysis samples, SoundEngine's waveBlock_) in input
        // samples, so the drawing is as wide as the engine's at every rate.
        const int rate = std::max(8000, sampleRateHz);
        const int analysis = SoundEngine::analysisRateFor(rate);
        block_ = std::clamp(std::max(1, fftSize_ / 512) * (rate / analysis), 1, kMaxBlock);
        dcR_ = std::exp(-2.0f * std::numbers::pi_v<float> * kDcCornerHz / static_cast<float>(rate));
        dcX_ = dcY_ = 0.0f;
    }
    const auto clean = [](float x) { return std::isfinite(x) ? std::clamp(x, -4.0f, 4.0f) : 0.0f; };
    const size_t stride = static_cast<size_t>(channels);
    for (int i = 0; i < frames; i++) {
        const float l = clean(interleaved[static_cast<size_t>(i) * stride]);
        const float r = channels > 1 ? clean(interleaved[static_cast<size_t>(i) * stride + 1]) : l;
        const float x = 0.5f * (l + r);
        float y = x - dcX_ + dcR_ * dcY_;
        if (std::fabs(y) < 1e-20f) y = 0.0f;
        dcX_ = x;
        dcY_ = y;
        mono_[static_cast<size_t>(head_)] = y * trim_;
        head_ = (head_ + 1) % kHistory;
    }
}

void Waveform::draw(float* out) {
    const int count = kPoints + kSlack;
    int at = (head_ - count * block_ + kHistory) % kHistory;
    for (int p = 0; p < count; p++) {
        float s = 0.0f;
        for (int j = 0; j < block_; j++) {
            s += mono_[static_cast<size_t>(at)];
            at = (at + 1) % kHistory;
        }
        raw_[static_cast<size_t>(p)] = s / static_cast<float>(block_);
    }
    // The offset that best matches the last drawing; the earliest wins a tie.
    int best = kSlack;
    if (hasPrev_) {
        float bestError = std::numeric_limits<float>::max();
        for (int d = 0; d <= kSlack; d++) {
            float error = 0.0f;
            for (int i = 0; i < kPoints; i++) {
                const size_t k = static_cast<size_t>(i);
                error += weight_[k] * std::fabs(raw_[k + static_cast<size_t>(d)] - prev_[k]);
            }
            if (error < bestError) {
                bestError = error;
                best = d;
            }
        }
    }
    for (int i = 0; i < kPoints; i++) {
        const size_t k = static_cast<size_t>(i);
        out[i] = raw_[k + static_cast<size_t>(best)];
        prev_[k] = out[i];
    }
    hasPrev_ = true;
}

}  // namespace geode::sound::interp
