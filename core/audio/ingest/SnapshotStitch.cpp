#include "audio/ingest/SnapshotStitch.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace geode::audio::ingest {

namespace {
constexpr int kChunk = 1024;
}

SnapshotStitch::SnapshotStitch()
    : snap_(static_cast<size_t>(kMaxFrames) * kMaxChannels),
      out_(static_cast<size_t>(kChunk) * kMaxChannels) {
    for (auto& g : grainData_) g.assign(static_cast<size_t>(kMaxFrames / 2) * kMaxChannels, 0.0f);
}

void SnapshotStitch::reset() {
    anchored_ = false;
    grains_ = {};
    untilGrain_ = 0;
}

void SnapshotStitch::startGrain() {
    const int length = std::max(2, (frames_ / 2) & ~1);
    random_ = random_ * 1664525u + 1013904223u;
    const int offset = static_cast<int>((random_ >> 8) % static_cast<uint32_t>(frames_ - length + 1));
    std::vector<float>& data = grainData_[static_cast<size_t>(nextSlot_)];
    const float* from = snap_.data() + static_cast<size_t>(offset) * channels_;
    for (int i = 0; i < length; ++i) {
        // A sine window: at 50 % overlap the two grains' squares sum to 1, so the power holds.
        const float w = std::sin(std::numbers::pi_v<float> * (static_cast<float>(i) + 0.5f) / static_cast<float>(length));
        for (int c = 0; c < channels_; ++c) {
            const size_t k = static_cast<size_t>(i) * channels_ + c;
            data[k] = from[k] * w;
        }
    }
    grains_[static_cast<size_t>(nextSlot_)] = {length, 0};
    nextSlot_ ^= 1;
    untilGrain_ = length / 2;
}

int64_t SnapshotStitch::spanFrames(int frames, int rate, int64_t nowNs, int64_t maxSpan) {
    const double since = static_cast<double>(nowNs - anchorNs_) * 1e-9;
    const bool broken = !anchored_ || rate != rate_ || since < 0.0 ||
                        static_cast<double>(nowNs - lastNs_) * 1e-9 * rate > static_cast<double>(maxSpan);
    lastNs_ = nowNs;
    if (broken) {
        // A new run: the snapshot is its own length, ending now.
        grains_ = {};
        untilGrain_ = 0;
        anchored_ = true;
        anchorNs_ = nowNs;
        sinceAnchor_ = 0;
        return frames;
    }
    const int64_t target = std::llround(since * rate);
    // Two snapshots arriving together (the Looper was busy) still each get one grain hop, so every snapshot
    // is heard; the clock takes the extra back from the next span.
    const int64_t span = std::max<int64_t>(target - sinceAnchor_, std::max(1, frames / 4));
    sinceAnchor_ += span;
    return span;
}

PcmRing::Written SnapshotStitch::write(PcmRing& ring, const void* data, size_t bytes, int encoding,
                                       int sourceChannels, int sampleRateHz, int64_t nowNs) {
    if (sourceChannels <= 0 || sourceChannels > kMaxChannels || sampleRateHz <= 0) return {};
    const int frames = PcmRing::decode(data, bytes, encoding, sourceChannels, snap_.data(), kMaxFrames);
    if (frames <= 0) return {};
    if (frames < kMinFrames) {
        reset();
        return ring.writeFloat(snap_.data(), frames, sourceChannels, sampleRateHz);
    }
    if (sourceChannels != channels_) reset();
    frames_ = frames;
    channels_ = sourceChannels;
    // A gap is filled only up to what a reader takes at once (the ring's runway), so no reader skips its start.
    const int64_t maxSpan = std::min<int64_t>(std::llround(kMaxGapSeconds * sampleRateHz), ring.maxWriteFrames());
    const int64_t span = spanFrames(frames, sampleRateHz, nowNs, maxSpan);
    rate_ = sampleRateHz;

    PcmRing::Written out;
    for (size_t i = 0; i < static_cast<size_t>(frames) * channels_; ++i) out.peak = std::max(out.peak, std::fabs(snap_[i]));
    int64_t left = span;
    while (left > 0) {
        const int n = static_cast<int>(std::min<int64_t>(left, kChunk));
        std::fill(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(n) * channels_, 0.0f);
        for (int i = 0; i < n; ++i) {
            if (untilGrain_ <= 0) startGrain();
            --untilGrain_;
            float* frame = out_.data() + static_cast<size_t>(i) * channels_;
            for (size_t g = 0; g < grains_.size(); ++g) {
                Grain& grain = grains_[g];
                if (grain.pos >= grain.length) continue;
                const float* src = grainData_[g].data() + static_cast<size_t>(grain.pos) * channels_;
                for (int c = 0; c < channels_; ++c) frame[c] += src[c];
                ++grain.pos;
            }
        }
        out.frames += ring.writeFloat(out_.data(), n, channels_, sampleRateHz).frames;
        left -= n;
    }
    return out;
}

}  // namespace geode::audio::ingest
