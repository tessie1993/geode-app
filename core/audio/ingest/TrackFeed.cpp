#include "audio/ingest/TrackFeed.hpp"

#include <algorithm>
#include <cstring>

namespace geode::audio::ingest {

namespace {
constexpr float kPcm16 = 32768.0f;
}  // namespace

float* TrackFeed::hold(size_t samples) {
    if (pending_.size() < samples) pending_.resize(samples);
    return pending_.data();
}

void TrackFeed::held(int frames, int channels, int rate) {
    at_ = 0;
    frames_ = frames;
    channels_ = channels;
    rate_ = rate;
}

void TrackFeed::pushDecoded(const uint8_t* data, size_t bytes, bool isFloat, int channels, int sampleRate,
                            int64_t ptsUs) {
    if (!data || bytes == 0 || channels <= 0) return;
    const size_t samples = isFloat ? bytes / 4 : bytes / 2;
    float* out = hold(samples);
    if (isFloat) {
        std::memcpy(out, data, samples * sizeof(float));
    } else {
        for (size_t i = 0; i < samples; ++i) {
            const auto v = static_cast<int16_t>(static_cast<uint16_t>(data[2 * i] | (data[2 * i + 1] << 8)));
            out[i] = static_cast<float>(v) / kPcm16;
        }
    }
    int count = static_cast<int>(samples / static_cast<size_t>(channels));
    // The extractor seeks to the keyframe at or before the start; drop what precedes it.
    const int64_t startUs = startMs_ * 1000;
    int from = 0;
    if (ptsUs < startUs) {
        // 64-bit until clamped: hours of pre-roll at a high rate pass 2^31 frames, and a wrapped cut reads past the chunk.
        from = static_cast<int>(std::clamp<int64_t>((startUs - ptsUs) * sampleRate / 1'000'000, 0, count));
        count -= from;
    }
    if (count > 0) {
        if (from > 0) {
            std::memmove(out, out + static_cast<size_t>(from) * channels,
                         static_cast<size_t>(count) * channels * sizeof(float));
        }
        held(count, channels, sampleRate);
    }
}

bool TrackFeed::pushAiff(AiffReader& aiff) {
    const int channels = aiff.channels();
    if (aiffSkip_ < 0) aiffSkip_ = startMs_ * aiff.sampleRate() / 1000;
    if (aiffBuf_.empty()) aiffBuf_.resize(kAiffChunk);
    const int n = aiff.readS16(aiffBuf_.data(), kAiffChunk);
    if (n <= 0) return false;
    int count = n / channels;
    int from = 0;
    if (aiffSkip_ > 0) {
        const int drop = static_cast<int>(std::min<int64_t>(aiffSkip_, count));
        aiffSkip_ -= drop;
        from = drop;
        count -= drop;
    }
    if (count <= 0) return true;
    float* out = hold(static_cast<size_t>(count) * channels);
    const int16_t* in = aiffBuf_.data() + static_cast<size_t>(from) * channels;
    for (int i = 0; i < count * channels; ++i) out[i] = static_cast<float>(in[i]) / kPcm16;
    held(count, channels, aiff.sampleRate());
    return true;
}

bool TrackFeed::deliver(Sink sink, void* context) {
    while (delivered_ < target_) {
        if (frames_ == 0) return false;
        const int n = static_cast<int>(std::min<int64_t>(frames_, target_ - delivered_));
        if (sink) sink(context, pending_.data() + static_cast<size_t>(at_) * channels_, n, channels_, rate_);
        at_ += n;
        frames_ -= n;
        delivered_ += n;
    }
    return true;
}

}  // namespace geode::audio::ingest
