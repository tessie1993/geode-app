#include "audio/dsp/Limiter.hpp"

#include <algorithm>
#include <cmath>

namespace geode::audio {

Limiter::Limiter(float sampleRate, int channels) : channels_(channels) {
    configure(sampleRate);
}

void Limiter::configure(float sampleRate) {
    lookahead_ = static_cast<size_t>(std::max(1.0f, kLookaheadSeconds * sampleRate));
    delay_.assign(lookahead_ * static_cast<size_t>(channels_), 0.0f);
    dequeValue_.assign(lookahead_, 0.0f);
    dequeIndex_.assign(lookahead_, 0);
    dequeHead_ = 0;
    dequeCount_ = 0;
    write_ = 0;
    gain_ = 1.0f;
    releaseCoefficient_ = 1.0f - std::exp(-1.0f / (kReleaseSeconds * sampleRate));
}

void Limiter::reset() {
    std::fill(delay_.begin(), delay_.end(), 0.0f);
    std::fill(dequeValue_.begin(), dequeValue_.end(), 0.0f);
    dequeHead_ = 0;
    dequeCount_ = 0;
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

        // Sliding-window maximum: the slot about to be overwritten is exactly the sample leaving the window,
        // so it is the only one that can expire off the front. Anything at the back no larger than the new
        // peak can never win while the new peak is still in the window, so it is dropped too.
        if (dequeCount_ > 0 && dequeIndex_[dequeHead_] == write_) {
            dequeHead_ = (dequeHead_ + 1) % lookahead_;
            --dequeCount_;
        }
        while (dequeCount_ > 0) {
            const size_t tail = (dequeHead_ + dequeCount_ - 1) % lookahead_;
            if (dequeValue_[tail] <= peak) {
                --dequeCount_;
            } else {
                break;
            }
        }
        const size_t tail = (dequeHead_ + dequeCount_) % lookahead_;
        dequeIndex_[tail] = write_;
        dequeValue_[tail] = peak;
        ++dequeCount_;
        const float windowPeak = dequeValue_[dequeHead_];

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
