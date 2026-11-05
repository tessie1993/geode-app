#include "audio/ingest/Ring.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include "audio/ingest/Downmix.hpp"
#include "sound/Silence.hpp"

namespace geode::audio::ingest {

namespace {

// Float PCM is copied as its bytes; Android is little-endian everywhere, and the integer formats below are
// assembled byte by byte, so only this one needs it.
static_assert(std::endian::native == std::endian::little, "float PCM is read as native floats");

constexpr int kMinCapacity = 256;
constexpr int kMaxCapacity = 1 << 24;

int roundUpPow2(int n) {
    n = std::clamp(n, kMinCapacity, kMaxCapacity);
    return static_cast<int>(std::bit_ceil(static_cast<unsigned>(n)));
}

// One sample of `encoding` at p, in -1..1. The integer scalings match what the Kotlin tap did (s16 / 32768),
// so a 16-bit stream reads bit-identically to before and the golden sound frames hold.
inline float sampleAt(const unsigned char* p, int encoding) {
    switch (encoding) {
        case kPcm16: {
            const auto v = static_cast<int16_t>(static_cast<uint16_t>(p[0] | (p[1] << 8)));
            return static_cast<float>(v) / 32768.0f;
        }
        case kPcm8:
            return static_cast<float>(static_cast<int>(p[0]) - 128) / 128.0f;
        case kPcmFloat: {
            float v;
            std::memcpy(&v, p, sizeof v);
            // A NaN or infinity would poison every filter and level downstream for the rest of the epoch.
            return std::isfinite(v) ? v : 0.0f;
        }
        case kPcm24Packed: {
            const uint32_t u = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                               (static_cast<uint32_t>(p[2]) << 16);
            const int32_t v = static_cast<int32_t>(u << 8) >> 8;   // sign-extend 24 bits
            return static_cast<float>(v) / 8388608.0f;
        }
        case kPcm32: {
            const uint32_t u = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            return static_cast<float>(static_cast<double>(static_cast<int32_t>(u)) / 2147483648.0);
        }
        default:
            return 0.0f;
    }
}

}  // namespace

int bytesPerSample(int encoding) {
    switch (encoding) {
        case kPcm16: return 2;
        case kPcm8: return 1;
        case kPcmFloat: return 4;
        case kPcm24Packed: return 3;
        case kPcm32: return 4;
        default: return 0;
    }
}

Ring::Ring(int capacityFrames, int maxChannels)
    : capacity_(roundUpPow2(capacityFrames)),
      stride_(std::clamp(maxChannels, 1, kMaxChannels)),
      // A quarter of the ring is the most one chunk may write: the margin a reader keeps from the pen. The
      // Kotlin SampleRing used the same divisor.
      runway_(capacity_ / 4),
      mask_(static_cast<int64_t>(capacity_) - 1),
      data_(static_cast<size_t>(capacity_) * static_cast<size_t>(stride_), 0.0f) {}

void Ring::publish(const Record& r) {
    const int64_t index = recordCount_.load(std::memory_order_relaxed);
    RecordSlot& s = slots_[index % kRecords];
    const auto i = static_cast<uint64_t>(index);
    s.seq.store(2 * i + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    s.start.store(r.start, std::memory_order_relaxed);
    s.sampleRate.store(r.sampleRate, std::memory_order_relaxed);
    s.channels.store(r.channels, std::memory_order_relaxed);
    s.sourceChannels.store(r.sourceChannels, std::memory_order_relaxed);
    s.mask.store(r.mask, std::memory_order_relaxed);
    s.epoch.store(r.epoch, std::memory_order_relaxed);
    s.seq.store(2 * i + 2, std::memory_order_release);
    recordCount_.store(index + 1, std::memory_order_release);
}

bool Ring::record(int64_t index, Record& out) const {
    if (index < 0 || index >= records()) return false;
    const RecordSlot& s = slots_[index % kRecords];
    const uint64_t want = 2 * static_cast<uint64_t>(index) + 2;
    if (s.seq.load(std::memory_order_acquire) != want) return false;
    out.start = s.start.load(std::memory_order_relaxed);
    out.sampleRate = s.sampleRate.load(std::memory_order_relaxed);
    out.channels = s.channels.load(std::memory_order_relaxed);
    out.sourceChannels = s.sourceChannels.load(std::memory_order_relaxed);
    out.mask = s.mask.load(std::memory_order_relaxed);
    out.epoch = s.epoch.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    return s.seq.load(std::memory_order_relaxed) == want;
}

void Ring::beginEpoch(int sampleRate, int channels, int channelMask) {
    // Always a new record, even for the same format: the restart is the point.
    const uint32_t epoch = current_.epoch + 1;
    hasFormat_ = false;
    current_.epoch = epoch;
    setFormat(sampleRate, channels, channelMask);
    if (!hasFormat_) {
        // An unusable format still marks the discontinuity, so readers reset on whatever comes next.
        current_ = Record{pen_, 0, 0, 0, 0, epoch};
        publish(current_);
    }
}

void Ring::setFormat(int sampleRate, int channels, int channelMask) {
    if (sampleRate <= 0 || channels <= 0) {
        hasFormat_ = false;
        return;
    }
    const int kept = std::min(channels, stride_);
    const uint32_t mask =
        downmix::firstSpeakers(downmix::effectiveMask(static_cast<uint32_t>(channelMask), channels), kept);
    const bool same = hasFormat_ && current_.sampleRate == sampleRate && current_.sourceChannels == channels &&
                      current_.mask == mask;
    if (same) return;
    current_ = Record{pen_, sampleRate, kept, channels, mask, current_.epoch};
    hasFormat_ = true;
    publish(current_);
}

size_t Ring::write(const void* bytes, size_t frames, int encoding) {
    const int width = bytesPerSample(encoding);
    if (!bytes || !hasFormat_ || width == 0 || frames == 0) return 0;
    const auto* src = static_cast<const unsigned char*>(bytes);
    const size_t frameBytes = static_cast<size_t>(width) * static_cast<size_t>(current_.sourceChannels);
    const int kept = current_.channels;
    size_t done = 0;
    while (done < frames) {
        const size_t n = std::min(frames - done, static_cast<size_t>(runway_));
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            float* dst = data_.data() + static_cast<size_t>((pen_ + static_cast<int64_t>(i)) & mask_) *
                                            static_cast<size_t>(stride_);
            const unsigned char* in = src + (done + i) * frameBytes;
            for (int c = 0; c < kept; ++c) {
                const float v = sampleAt(in + static_cast<size_t>(c) * static_cast<size_t>(width), encoding);
                dst[c] = v;
                peak = std::max(peak, std::fabs(v));
            }
        }
        pen_ += static_cast<int64_t>(n);
        done += n;
        written_.store(pen_, std::memory_order_release);
        if (peak > geode::sound::silence::kPeak) audibleAt_.store(pen_, std::memory_order_release);
    }
    return frames;
}

size_t Ring::writeBytes(const void* bytes, size_t byteCount, int encoding) {
    const int width = bytesPerSample(encoding);
    if (!hasFormat_ || width == 0) return 0;
    const size_t frameBytes = static_cast<size_t>(width) * static_cast<size_t>(current_.sourceChannels);
    return write(bytes, byteCount / frameBytes, encoding);
}

bool Ring::resting(int64_t nowMs) {
    int64_t heard = heardAtMs_.load(std::memory_order_relaxed);
    if (heard == kNeverPolled) {
        heard = nowMs;
        heardAtMs_.store(heard, std::memory_order_relaxed);
    }
    const int64_t audible = audibleAt_.load(std::memory_order_acquire);
    if (seenAudible_.exchange(audible, std::memory_order_relaxed) != audible) {
        heard = std::max(heard, nowMs);
        heardAtMs_.store(heard, std::memory_order_relaxed);
    }
    return geode::sound::silence::resting(nowMs, heard);
}

}  // namespace geode::audio::ingest
