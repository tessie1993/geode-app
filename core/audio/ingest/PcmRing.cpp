#include "audio/ingest/PcmRing.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace geode::audio::ingest {

namespace {

// memcpy reads: a byte block from Java has no alignment to rely on.
float readFloat32(const unsigned char* p) {
    float v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

float readSigned16(const unsigned char* p) {
    int16_t v;
    std::memcpy(&v, p, sizeof(v));
    return static_cast<float>(v) / 32768.0f;
}

float readUnsigned8(const unsigned char* p) { return (static_cast<float>(*p) - 128.0f) / 128.0f; }

uint64_t packFormat(int sampleRateHz, int sourceChannels) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(sampleRateHz)) << 32) | static_cast<uint32_t>(sourceChannels);
}

}  // namespace

bool PcmRing::valid(int capacityFrames, int channelCount, int maxWriteFrames) {
    if (capacityFrames <= 0 || (capacityFrames & (capacityFrames - 1)) != 0 || channelCount <= 0) return false;
    const int runway = maxWriteFrames == 0 ? capacityFrames / kRunwayDivisor : maxWriteFrames;
    return runway >= 1 && runway <= capacityFrames;
}

PcmRing::PcmRing(int capacityFrames, int channelCount, int maxWriteFrames)
    : capacity_(capacityFrames),
      channels_(channelCount),
      maxWrite_(maxWriteFrames > 0 ? maxWriteFrames : capacityFrames / kRunwayDivisor),
      mask_(static_cast<int64_t>(capacityFrames) - 1),
      samples_(static_cast<size_t>(capacityFrames) * static_cast<size_t>(channelCount), 0.0f) {}

int64_t PcmRing::writtenFrames() const {
    // The epoch first: the head only grows, so it is never below the start read before it.
    const uint64_t word = epoch_.load();
    return head_.load() - startOf(word);
}

void PcmRing::beginEpoch() {
    const uint64_t number = (epoch_.load() >> kEpochShift) + 1;
    epoch_.store((number << kEpochShift) | (static_cast<uint64_t>(head_.load()) & kStartMask));
}

int64_t PcmRing::oldestAvailable(int64_t head, int64_t epochStart) const {
    return std::max(epochStart, head + maxWrite_ - capacity_);
}

void PcmRing::mark(int64_t head, uint64_t format) {
    const uint32_t count = markCount_.load();
    // Compared with the log's own tail, not a copy kept by the writer: when two sources overlap at a
    // hand-over and one mark is lost, the next write puts it back.
    if (count > 0 && marks_[(count - 1) % kMarks].format.load() == format) return;
    Mark& next = marks_[count % kMarks];
    next.start.store(head);
    next.format.store(format);
    markCount_.store(count + 1);
}

PcmRing::Run PcmRing::runAt(int64_t pos, int64_t limit) const {
    for (;;) {
        const uint32_t count = markCount_.load();
        // One fewer than the log holds: the oldest slot is the one the writer fills next, maybe right now.
        const uint32_t kept = std::min(count, kMarks - 1);
        Run run;
        run.end = limit;
        uint64_t format = 0;
        for (uint32_t i = 0; i < kept; ++i) {
            const Mark& m = marks_[(count - 1 - i) % kMarks];
            const int64_t start = m.start.load();
            format = m.format.load();
            if (start <= pos) break;
            run.end = std::min(run.end, start);
        }
        // A mark added meanwhile may have reused a slot read above: read again.
        if (markCount_.load() != count) continue;
        run.sampleRateHz = static_cast<int>(format >> 32);
        run.sourceChannels = static_cast<int>(format & 0xffffffffu);
        return run;
    }
}

template <typename Read>
PcmRing::Written PcmRing::put(const unsigned char* data, int frames, int sourceChannels, int sampleRateHz,
                              size_t width, Read read) {
    Written out;
    if (!data || frames <= 0 || sourceChannels <= 0 || sampleRateHz <= 0) return out;
    const uint64_t format = packFormat(sampleRateHz, sourceChannels);
    const int stored = std::min(channels_, sourceChannels);
    const size_t frameBytes = static_cast<size_t>(sourceChannels) * width;
    const size_t stride = static_cast<size_t>(channels_);
    int64_t head = head_.load();
    while (out.frames < frames) {
        const int block = std::min(frames - out.frames, maxWrite_);
        mark(head, format);
        for (int i = 0; i < block; ++i) {
            float* slot = &samples_[static_cast<size_t>((head + i) & mask_) * stride];
            for (int c = 0; c < stored; ++c) {
                const float v = read(data + static_cast<size_t>(c) * width);
                slot[c] = v;
                const float magnitude = std::fabs(v);
                if (magnitude > out.peak) out.peak = magnitude;
            }
            for (int c = stored; c < channels_; ++c) slot[c] = 0.0f;
            data += frameBytes;
        }
        head += block;
        // Published a runway at a time, so a reader's oldest safe frame holds while a long block lands.
        head_.store(head);
        out.frames += block;
    }
    return out;
}

PcmRing::Written PcmRing::writeFloat(const float* interleaved, int frames, int sourceChannels, int sampleRateHz) {
    return put(reinterpret_cast<const unsigned char*>(interleaved), frames, sourceChannels, sampleRateHz,
               sizeof(float), readFloat32);
}

PcmRing::Written PcmRing::writeInt16(const int16_t* interleaved, int frames, int sourceChannels, int sampleRateHz) {
    return put(reinterpret_cast<const unsigned char*>(interleaved), frames, sourceChannels, sampleRateHz,
               sizeof(int16_t), readSigned16);
}

PcmRing::Written PcmRing::writeBytes(const void* data, size_t bytes, int encoding, int sourceChannels,
                                     int sampleRateHz) {
    if (sourceChannels <= 0) return {};
    const auto* p = static_cast<const unsigned char*>(data);
    const auto framesOf = [&](size_t width) {
        return static_cast<int>(std::min<size_t>(bytes / (width * static_cast<size_t>(sourceChannels)), INT_MAX));
    };
    switch (encoding) {
        case kSigned16:
            return put(p, framesOf(sizeof(int16_t)), sourceChannels, sampleRateHz, sizeof(int16_t), readSigned16);
        case kUnsigned8:
            return put(p, framesOf(1), sourceChannels, sampleRateHz, 1, readUnsigned8);
        case kFloat32:
            return put(p, framesOf(sizeof(float)), sourceChannels, sampleRateHz, sizeof(float), readFloat32);
        default:
            return {};
    }
}

void PcmRing::copyOut(int64_t first, int count, bool monoSource, float* interleaved) const {
    const size_t stride = static_cast<size_t>(channels_);
    for (int i = 0; i < count; ++i) {
        const float* slot = &samples_[static_cast<size_t>((first + i) & mask_) * stride];
        float* out = interleaved + static_cast<size_t>(i) * stride;
        // A mono source left the other channels silent; handing its one channel to all of them reads
        // centred and narrow instead of hard left.
        for (int c = 0; c < channels_; ++c) out[c] = slot[monoSource ? 0 : c];
    }
}

bool PcmRing::snapshotLatest(float* interleaved, int frames) const {
    if (!interleaved || frames <= 0 || frames > capacity_) return false;
    const uint64_t word = epoch_.load();
    const int64_t head = head_.load();
    if (head - startOf(word) < frames) return false;
    copyOut(head - frames, frames, false, interleaved);
    return true;
}

Cursor::Cursor(const PcmRing& ring, int maxFrames)
    : ring_(ring),
      maxFrames_(std::min(maxFrames > 0 ? maxFrames : ring.maxWriteFrames(), ring.capacityFrames())),
      out_(static_cast<size_t>(maxFrames_) * static_cast<size_t>(ring.channelCount()), 0.0f) {}

Cursor::Take Cursor::take() {
    Take out;
    // The epoch, then the head, each once: everything below this head is published, and a head read
    // after the epoch is never below that epoch's start.
    const uint64_t word = ring_.epoch_.load();
    const int64_t head = ring_.head_.load();
    const int64_t oldest = ring_.oldestAvailable(head, PcmRing::startOf(word));
    if (pending_ || word != epoch_ || read_ > head) {
        pending_ = false;
        epoch_ = word;
        read_ = std::max(oldest, head - maxFrames_);
        out.restarted = true;
    }
    // A stall longer than the ring holds: skip to the oldest frame still safe to read.
    if (read_ < oldest) read_ = oldest;
    // A stall longer than one take: keep the newest audio, which is what is playing now.
    if (head - read_ > maxFrames_) read_ = head - maxFrames_;
    if (head <= read_) return out;
    const PcmRing::Run run = ring_.runAt(read_, head);
    const int count = static_cast<int>(std::min(head, run.end) - read_);
    if (count <= 0) return out;
    ring_.copyOut(read_, count, run.sourceChannels == 1, out_.data());
    read_ += count;
    out.frames = count;
    out.sampleRateHz = run.sampleRateHz;
    return out;
}

}  // namespace geode::audio::ingest
