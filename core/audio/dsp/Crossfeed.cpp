#include "audio/dsp/Crossfeed.hpp"

#include <cmath>

namespace geode::audio {

Crossfeed::Crossfeed(float sampleRate) {
    const auto c = Biquad::lowPass(sampleRate, kCutoffHz, 0.7071f);
    for (auto& f : lowPass_) f.set(c);
    feed_ = std::pow(10.0f, kFeedDb / 20.0f);
    // The direct path gives up what the feed adds, so the summed level stays where it was.
    direct_ = 1.0f / (1.0f + feed_);
    feed_ *= direct_;
}

void Crossfeed::reset() {
    for (auto& f : lowPass_) f.reset();
}

void Crossfeed::process(float* interleaved, size_t frames, int channels) {
    if (channels != 2 || !enabled_.load(std::memory_order_relaxed)) return;
    for (size_t i = 0; i < frames; ++i) {
        float& left = interleaved[i * 2];
        float& right = interleaved[i * 2 + 1];
        const float leftFeed = lowPass_[0].process(left);
        const float rightFeed = lowPass_[1].process(right);
        const float outLeft = left * direct_ + rightFeed * feed_;
        const float outRight = right * direct_ + leftFeed * feed_;
        left = outLeft;
        right = outRight;
    }
}

}  // namespace geode::audio
