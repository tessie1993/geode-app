#include "audio/dsp/PcmResampler.hpp"

#include <algorithm>

// Bit-identical to the JVM, which never fuses a multiply and an add.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace geode::audio {

PcmResampler::PcmResampler(int inRateHz, int outRateHz, int channels)
    : inRate_(inRateHz), outRate_(outRateHz), channels_(channels),
      sums_(static_cast<size_t>(channels), 0), last_(static_cast<size_t>(channels), 0) {}

size_t PcmResampler::maxOutFrames(size_t frames) const {
    if (inRate_ == outRate_) return frames;
    return static_cast<size_t>(static_cast<int64_t>(frames) * outRate_ / inRate_ + 2);
}

size_t PcmResampler::process(const int16_t* in, size_t frames, int16_t* out) {
    if (inRate_ == outRate_) {
        std::copy(in, in + frames * static_cast<size_t>(channels_), out);
        return frames;
    }
    return outRate_ < inRate_ ? down(in, frames, out) : up(in, frames, out);
}

size_t PcmResampler::down(const int16_t* in, size_t frames, int16_t* out) {
    size_t written = 0;
    for (size_t f = 0; f < frames; ++f) {
        for (int c = 0; c < channels_; ++c) sums_[c] += in[f * channels_ + c];
        ++summed_;
        ++inFrames_;
        // Output frame k covers input frames [k * in / out, (k + 1) * in / out).
        if (inFrames_ * outRate_ >= (outFrames_ + 1) * inRate_) {
            for (int c = 0; c < channels_; ++c) {
                out[written++] = static_cast<int16_t>(sums_[c] / summed_);
                sums_[c] = 0;
            }
            summed_ = 0;
            ++outFrames_;
        }
    }
    return written / static_cast<size_t>(channels_);
}

size_t PcmResampler::up(const int16_t* in, size_t frames, int16_t* out) {
    if (frames == 0) return 0;
    const int64_t first = inFrames_ - (inFrames_ > 0 ? 1 : 0);
    const int64_t end = inFrames_ + static_cast<int64_t>(frames);
    auto sampleAt = [&](int64_t absFrame, int c) -> int16_t {
        return absFrame < inFrames_ ? last_[c] : in[(absFrame - inFrames_) * channels_ + c];
    };
    size_t written = 0;
    // Output frame k sits at input position k * in / out; it needs input frames i0 and i0 + 1.
    int64_t num = outFrames_ * inRate_;
    while (num / outRate_ + 1 < end) {
        const int64_t i0 = num / outRate_;
        if (i0 >= first) {
            const float frac = static_cast<float>(num % outRate_) / static_cast<float>(outRate_);
            for (int c = 0; c < channels_; ++c) {
                const float a = sampleAt(i0, c);
                const float b = sampleAt(i0 + 1, c);
                const float step = (b - a) * frac;
                out[written++] = static_cast<int16_t>(static_cast<int32_t>(a + step));
            }
        }
        ++outFrames_;
        num = outFrames_ * inRate_;
    }
    for (int c = 0; c < channels_; ++c) last_[c] = in[(frames - 1) * channels_ + c];
    inFrames_ = end;
    return written / static_cast<size_t>(channels_);
}

}  // namespace geode::audio
