#include "audio/ingest/LayerMix.hpp"

#include <algorithm>
#include <cmath>

namespace geode::audio::ingest {

namespace {

// Takes per pull: one per rate in the layer's new audio, as the renderer's own feed takes.
constexpr int kRunsPerPull = 4;
// The most one alone block holds; what is left waits for the next call.
constexpr int kAloneBlockFrames = 1 << 15;

}  // namespace

LayerMix::LayerMix(const PcmRing& layer) : cursor_(layer), channels_(layer.channelCount()) {
    queue_.reserve(static_cast<size_t>(layer.capacityFrames()) * static_cast<size_t>(channels_));
    block_.resize(static_cast<size_t>(kAloneBlockFrames) * static_cast<size_t>(channels_));
}

void LayerMix::clear() {
    queue_.clear();
    phase_ = 0.0;
    primed_ = false;
}

void LayerMix::pull() {
    for (int run = 0; run < kRunsPerPull; ++run) {
        const Cursor::Take take = cursor_.take();
        if (take.restarted) {
            clear();
            heardAlone_ = false;
        }
        if (take.frames <= 0) break;
        if (take.sampleRateHz != rate_) {
            clear();
            rate_ = take.sampleRateHz;
        }
        const float* in = cursor_.interleaved();
        queue_.insert(queue_.end(), in, in + static_cast<size_t>(take.frames) * static_cast<size_t>(channels_));
        sincePrimary_ += take.frames;
    }
}

int LayerMix::render(float* dest, int maxFrames, int destChannels, bool add) {
    const int64_t have = waiting();
    const double step = static_cast<double>(rate_) / static_cast<double>(outRate_);
    // Frame k reads the queue at phase_ + k * step and the frame after it, which must be there.
    const double room = static_cast<double>(have - 1) - phase_;
    if (room <= 0.0) return 0;
    const int count = static_cast<int>(std::min<double>(maxFrames, std::ceil(room / step)));
    const size_t stride = static_cast<size_t>(channels_);
    for (int k = 0; k < count; ++k) {
        const double pos = phase_ + static_cast<double>(k) * step;
        const auto index = static_cast<size_t>(pos);
        const auto frac = static_cast<float>(pos - static_cast<double>(index));
        const float* a = &queue_[index * stride];
        float* out = dest + static_cast<size_t>(k) * static_cast<size_t>(destChannels);
        for (int c = 0; c < destChannels; ++c) {
            const int source = std::min(c, channels_ - 1);
            const float v = a[source] + (a[stride + static_cast<size_t>(source)] - a[source]) * frac;
            out[c] = add ? out[c] + v : v;
        }
    }
    phase_ += static_cast<double>(count) * step;
    const int64_t used = std::min<int64_t>(static_cast<int64_t>(phase_), have);
    phase_ -= static_cast<double>(used);
    queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(used * channels_));
    return count;
}

void LayerMix::trim() {
    const int64_t most = static_cast<int64_t>(kMaxSeconds * rate_);
    if (waiting() <= most) return;
    const int64_t drop = waiting() - static_cast<int64_t>(kPrimeSeconds * rate_);
    queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(drop * channels_));
}

const float* LayerMix::mixed(const float* interleaved, int frames, int channels, int rateHz) {
    pull();
    alone_ = false;
    heardAlone_ = false;
    sincePrimary_ = 0;
    if (!interleaved || frames <= 0 || channels <= 0 || rateHz <= 0) return interleaved;
    outRate_ = rateHz;
    if (rate_ <= 0) return interleaved;
    if (!primed_) {
        if (waiting() < static_cast<int64_t>(kPrimeSeconds * rate_)) {
            trim();
            return interleaved;
        }
        primed_ = true;
    }
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(channels);
    if (block_.size() < samples) block_.resize(samples);
    std::copy(interleaved, interleaved + samples, block_.begin());
    // An underrun leaves the rest of the block the primary's alone, and the layer waits to be primed again.
    if (render(block_.data(), frames, channels, true) < frames) primed_ = false;
    trim();
    return block_.data();
}

LayerMix::Alone LayerMix::takeAlone() {
    pull();
    if (!alone_) {
        if (rate_ <= 0 || sincePrimary_ < static_cast<int64_t>(kAloneSeconds * rate_)) return {};
        alone_ = true;
        primed_ = false;
    }
    if (rate_ <= 0) return {};
    if (outRate_ <= 0) outRate_ = rate_;
    const int frames = render(block_.data(), kAloneBlockFrames, channels_, false);
    if (frames <= 0) return {};
    heardAlone_ = true;
    return {block_.data(), frames, channels_, outRate_};
}

}  // namespace geode::audio::ingest
