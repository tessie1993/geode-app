#include "audio/output/OutputFifo.hpp"

#include <algorithm>
#include <cstring>

namespace geode::audio::output {

namespace {

// The samples are atomics only to make a burst cut by a flush defined; they must stay plain moves.
static_assert(std::atomic<float>::is_always_lock_free);

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

int64_t storageFrames(int capacityFrames) {
    int64_t size = 1;
    while (size < capacityFrames) size <<= 1;
    return size;
}

constexpr int64_t kNanosPerSecond = 1'000'000'000;
constexpr int64_t kMicrosPerSecond = 1'000'000;
// Past this the clock after stop() has covered any buffer there could be; also keeps the multiply small.
constexpr int64_t kMaxRunOnNs = 3600 * kNanosPerSecond;

}  // namespace

bool OutputFifo::supports(int encoding, int channelCount, int sampleRateHz) {
    return bytesPerFrame(encoding, channelCount) > 0 && sampleRateHz >= kMinRateHz && sampleRateHz <= kMaxRateHz;
}

int OutputFifo::bytesPerFrame(int encoding, int channelCount) {
    if (channelCount < 1 || channelCount > kMaxChannels) return 0;
    if (encoding == kSigned16) return 2 * channelCount;
    if (encoding == kFloat32) return 4 * channelCount;
    return 0;
}

int OutputFifo::capacityFrames(int sampleRateHz, int channelCount, int encoding, int bufferBytes) {
    const int frameBytes = bytesPerFrame(encoding, channelCount);
    if (frameBytes <= 0 || sampleRateHz <= 0) return 0;
    const int64_t rate = sampleRateHz;
    const int64_t asked = bufferBytes > 0 ? bufferBytes / frameBytes : rate * kDefaultLatencyMs / 1000;
    const int64_t least = rate * kMinLatencyMs / 1000;
    const int64_t most = rate * kMaxLatencyMs / 1000;
    return static_cast<int>(std::clamp(asked, least, most));
}

OutputFifo::OutputFifo(int sampleRateHz, int channelCount, int capacityFrames)
    : rate_(sampleRateHz),
      channels_(channelCount),
      capacity_(capacityFrames),
      mask_(storageFrames(capacityFrames) - 1),
      rampStep_(1000.0f / (static_cast<float>(sampleRateHz) * static_cast<float>(kRampMs))),
      samples_(static_cast<size_t>(storageFrames(capacityFrames)) * static_cast<size_t>(channelCount)) {}

template <typename Sample>
int OutputFifo::put(const unsigned char* data, int frames, size_t width, Sample sample) {
    if (!data || frames <= 0) return 0;
    const int64_t write = write_.load(std::memory_order_relaxed);
    // Flushed frames the consumer has not stepped over yet take no room: it never plays them.
    const int64_t oldest = std::max(read_.load(std::memory_order_acquire), floor_.load(std::memory_order_relaxed));
    const int64_t room = static_cast<int64_t>(capacity_) - (write - oldest);
    const int count = static_cast<int>(std::min<int64_t>(frames, std::max<int64_t>(room, 0)));
    const size_t channels = static_cast<size_t>(channels_);
    for (int i = 0; i < count; ++i) {
        const size_t slot = static_cast<size_t>((write + i) & mask_) * channels;
        for (size_t c = 0; c < channels; ++c) {
            samples_[slot + c].store(sample(data), std::memory_order_relaxed);
            data += width;
        }
    }
    if (count > 0) write_.store(write + count, std::memory_order_release);
    return count;
}

int OutputFifo::writeFloat(const float* interleaved, int frames) {
    return put(reinterpret_cast<const unsigned char*>(interleaved), frames, sizeof(float), readFloat32);
}

int OutputFifo::writeInt16(const int16_t* interleaved, int frames) {
    return put(reinterpret_cast<const unsigned char*>(interleaved), frames, sizeof(int16_t), readSigned16);
}

int OutputFifo::writeBytes(const void* data, size_t bytes, int encoding) {
    const int frameBytes = bytesPerFrame(encoding, channels_);
    if (frameBytes <= 0) return 0;
    const size_t whole = bytes / static_cast<size_t>(frameBytes);
    const int frames = static_cast<int>(std::min<size_t>(whole, static_cast<size_t>(capacity_)));
    const auto* p = static_cast<const unsigned char*>(data);
    if (encoding == kSigned16) return put(p, frames, sizeof(int16_t), readSigned16);
    return put(p, frames, sizeof(float), readFloat32);
}

void OutputFifo::flush() {
    floor_.store(write_.load(std::memory_order_relaxed), std::memory_order_release);
    stopped_.store(false, std::memory_order_relaxed);
    lastPosition_ = 0;
    stopPosition_ = 0;
    advancePending_ = playing_;
    stallWatching_ = false;
}

void OutputFifo::play(int64_t nowNs) {
    if (playing_ && stopped_.load(std::memory_order_relaxed)) return;
    playing_ = true;
    playStartNs_ = nowNs;
    advancePending_ = true;
    stallWatching_ = false;
    // After stop() the position also runs by the clock; it stood still while paused.
    stopPosition_ = lastPosition_;
    stopNs_ = nowNs;
}

void OutputFifo::pause() {
    if (playing_ && stopped_.load(std::memory_order_relaxed)) return;
    playing_ = false;
    stallWatching_ = false;
}

void OutputFifo::stop(int64_t nowNs) {
    if (stopped_.load(std::memory_order_relaxed)) return;
    // Released after the last write, so a consumer that sees it also sees every frame written.
    stopped_.store(true, std::memory_order_release);
    stopPosition_ = lastPosition_;
    stopNs_ = nowNs;
}

void OutputFifo::setVolume(float volume) {
    // !(v > 0) also catches a NaN.
    const float clamped = !(volume > 0.0f) ? 0.0f : std::min(volume, 1.0f);
    gainTarget_.store(clamped, std::memory_order_relaxed);
}

void OutputFifo::streamRestarted() {
    // What the old stream took and never played is counted as played: it is gone either way.
    delivered_.store(0, std::memory_order_relaxed);
    lead_.store(-framesPlayed(), std::memory_order_release);
    underrun_ = false;
}

void OutputFifo::read(float* out, int frames) {
    if (!out || frames <= 0) return;
    const int64_t floor = floor_.load(std::memory_order_acquire);
    const int64_t start = std::max(read_.load(std::memory_order_relaxed), floor);
    const int64_t waiting = write_.load(std::memory_order_acquire) - start;
    int take = static_cast<int>(std::min<int64_t>(frames, std::max<int64_t>(waiting, 0)));

    const float target = gainTarget_.load(std::memory_order_relaxed);
    if (gain_ < 0.0f) gain_ = target;
    const size_t channels = static_cast<size_t>(channels_);
    size_t o = 0;
    for (int i = 0; i < take; ++i) {
        if (gain_ < target) {
            gain_ = std::min(target, gain_ + rampStep_);
        } else if (gain_ > target) {
            gain_ = std::max(target, gain_ - rampStep_);
        }
        const size_t slot = static_cast<size_t>((start + i) & mask_) * channels;
        for (size_t c = 0; c < channels; ++c) out[o++] = samples_[slot + c].load(std::memory_order_relaxed) * gain_;
    }

    // A flush while copying: those frames are gone and the producer may have reused their slots, so
    // none of the copy is played. The read position stays; the floor is ahead of it now.
    const int64_t floorNow = floor_.load(std::memory_order_acquire);
    const bool flushed = floorNow != floor;
    if (flushed) take = 0;
    const int silent = frames - take;
    if (silent > 0) {
        std::fill(out + static_cast<size_t>(take) * channels, out + static_cast<size_t>(frames) * channels, 0.0f);
        // The ramp runs on wall time, so it also moves through the silence.
        const float travel = rampStep_ * static_cast<float>(silent);
        gain_ = gain_ < target ? std::min(target, gain_ + travel) : std::max(target, gain_ - travel);
    }
    if (!flushed) read_.store(start + take, std::memory_order_release);

    const int64_t readSinceFlush = flushed ? 0 : start + take - floor;
    const int64_t before = delivered_.load(std::memory_order_relaxed);
    delivered_.store(before + frames, std::memory_order_relaxed);
    // Only the silence ahead of the last frame read counts: silence behind it pushes that frame out
    // to the speaker like any other, so it must not hold the position back.
    if (take > 0) lead_.store(before + take - readSinceFlush, std::memory_order_release);

    // One underrun per dry spell, and only mid-stream: not before the first frame, not after the last.
    if (silent == 0 || flushed) {
        underrun_ = false;
    } else if (!underrun_ && readSinceFlush > 0 && !stopped_.load(std::memory_order_relaxed)) {
        underrun_ = true;
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
}

int64_t OutputFifo::framesWritten() const {
    // The floor first: the write position only grows, so it is never below a floor read before it.
    const int64_t floor = floor_.load(std::memory_order_acquire);
    return write_.load(std::memory_order_acquire) - floor;
}

int64_t OutputFifo::framesPlayed() const {
    const int64_t floor = floor_.load(std::memory_order_acquire);
    return std::max(read_.load(std::memory_order_acquire), floor) - floor;
}

int64_t OutputFifo::framesIn(int64_t ns) const {
    return std::clamp<int64_t>(ns, 0, kMaxRunOnNs) * rate_ / kNanosPerSecond;
}

int64_t OutputFifo::positionUs(int64_t nowNs, const StreamTime& stream, int64_t latencyFrames) {
    const int64_t played = framesPlayed();
    int64_t position = lastPosition_;
    if (playing_) {
        // A timestamp from before this play() describes the stream before the pause, and one from long
        // ago or from the future cannot be run on from.
        const bool fresh = stream.valid && stream.timeNs >= playStartNs_ && stream.timeNs <= nowNs &&
                           nowNs - stream.timeNs <= kMaxExtrapolateNs;
        if (fresh) {
            const int64_t heardOfStream = stream.framePosition + framesIn(nowNs - stream.timeNs);
            position = heardOfStream - lead_.load(std::memory_order_acquire);
        } else {
            position = played - std::max<int64_t>(latencyFrames, 0);
        }
        position = std::clamp<int64_t>(position, 0, played);
        if (stopped_.load(std::memory_order_relaxed)) {
            const int64_t byClock = stopPosition_ + framesIn(nowNs - stopNs_);
            position = std::max(position, std::min(byClock, framesWritten()));
        }
    }
    position = std::max(position, lastPosition_);
    if (advancePending_ && position > lastPosition_) {
        advancePending_ = false;
        events_ |= kEventAdvancing;
    }
    lastPosition_ = position;
    return position * kMicrosPerSecond / rate_;
}

bool OutputFifo::drained() const {
    // stopped_ first: the frames written before stop() are then visible too.
    return stopped_.load(std::memory_order_acquire) && framesPlayed() >= framesWritten();
}

bool OutputFifo::ended() const {
    return stopped_.load(std::memory_order_relaxed) && lastPosition_ >= framesWritten();
}

int OutputFifo::takeEvents() {
    int events = events_;
    events_ = 0;
    const int64_t underruns = underruns_.load(std::memory_order_relaxed);
    if (underruns != underrunsSeen_) {
        underrunsSeen_ = underruns;
        events |= kEventUnderrun;
    }
    return events;
}

bool OutputFifo::stalled(int64_t nowNs) {
    const int64_t delivered = delivered_.load(std::memory_order_relaxed);
    if (!playing_ || pendingFrames() <= 0 || !stallWatching_ || delivered != stallDelivered_) {
        stallWatching_ = playing_;
        stallDelivered_ = delivered;
        stallSinceNs_ = nowNs;
        return false;
    }
    return nowNs - stallSinceNs_ >= kStallNs;
}

}  // namespace geode::audio::output
