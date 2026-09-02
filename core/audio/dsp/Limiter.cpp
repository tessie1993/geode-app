#include "audio/dsp/Limiter.hpp"

#include <algorithm>
#include <cmath>

namespace geode::audio {

Limiter::Limiter(float sampleRate, int channels)
    : channels_(channels),
      lookahead_(static_cast<size_t>(std::max(1.0f, kLookaheadSeconds * sampleRate))),
      delay_(lookahead_ * static_cast<size_t>(channels), 0.0f),
      peaks_(lookahead_, 0.0f),
      releaseCoefficient_(1.0f - std::exp(-1.0f / (kReleaseSeconds * sampleRate))) {}

void Limiter::reset() {
    std::fill(delay_.begin(), delay_.end(), 0.0f);
    std::fill(peaks_.begin(), peaks_.end(), 0.0f);
    write_ = 0;
    gain_ = 1.0f;
}

void Limiter::process(float* interleaved, size_t frames) {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    const size_t ch = static_cast<size_t>(channels_);
    for (size_t i = 0; i < frames; ++i) {
        float* in = interleaved + i * ch;
        float peak = 0.0f;
        for (size_t c = 0; c < ch; ++c) peak = std::max(peak, std::fabs(in[c]));
        peaks_[write_] = peak;
        // The loudest sample inside the lookahead window decides the gain the delayed sample leaves with.
        float windowPeak = 0.0f;
        for (float p : peaks_) windowPeak = std::max(windowPeak, p);
        const float needed = windowPeak > kCeiling ? kCeiling / windowPeak : 1.0f;
        if (needed < gain_) {
            gain_ = needed;
        } else {
            gain_ += (needed - gain_) * releaseCoefficient_;
        }
        float* slot = delay_.data() + write_ * ch;
        for (size_t c = 0; c < ch; ++c) {
            const float delayed = slot[c];
            slot[c] = in[c];
            in[c] = std::clamp(delayed * gain_, -kCeiling, kCeiling);
        }
        write_ = (write_ + 1) % lookahead_;
    }
}

}  // namespace geode::audio
