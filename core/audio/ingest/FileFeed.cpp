#include "audio/ingest/FileFeed.hpp"

#include <algorithm>

namespace geode::audio::ingest {

namespace {

// Stereo frames one cursor read copies at most; a frame's audio takes a few reads at high rates.
constexpr int kReadFrames = 4096;
constexpr int64_t kMicrosPerSecond = 1'000'000;

}  // namespace

FileFeed::FileFeed(int64_t startUs)
    : startUs_(std::max<int64_t>(startUs, 0)),
      ring_(std::make_unique<Ring>(kCapacityFrames, kMaxChannels)),
      // Made before the first write, from the oldest position: the cursor's first read is the range's
      // first kept sample, ring position 0, which is where frame 0 starts.
      cursor_(std::make_unique<Cursor>(*ring_, Cursor::Start::Oldest)),
      stereo_(static_cast<size_t>(kReadFrames) * 2, 0.0f) {}

int64_t FileFeed::write(const void* bytes, size_t byteCount, int encoding, int sampleRate, int channels,
                        int64_t ptsUs) {
    const int bps = bytesPerSample(encoding);
    if (!bytes || bps <= 0 || sampleRate <= 0 || channels <= 0) return -1;
    const size_t frameBytes = static_cast<size_t>(bps) * static_cast<size_t>(channels);
    const auto frames = static_cast<int64_t>(byteCount / frameBytes);

    if (rate_ == 0) {
        // One file is one epoch: the export never seeks inside its range.
        ring_->beginEpoch(sampleRate, channels, 0);
        clockRate_ = sampleRate;
    } else if (sampleRate != rate_ || channels != channels_) {
        // A decoder that changes format part-way continues the same stream (no restart for the engine).
        ring_->setFormat(sampleRate, channels, 0);
        if (sampleRate != rate_) sourceAt_ = sourceAt_ * sampleRate / rate_;
    }
    rate_ = sampleRate;
    channels_ = channels;

    // Where this chunk sits in the source, in frames at its own rate, and how much of it precedes the start.
    const int64_t chunkAt = ptsUs >= 0 ? ptsUs * sampleRate / kMicrosPerSecond : sourceAt_;
    const int64_t startAt = startUs_ * sampleRate / kMicrosPerSecond;
    const int64_t drop = std::clamp<int64_t>(startAt - chunkAt, 0, frames);
    sourceAt_ = chunkAt + frames;

    const int64_t kept = frames - drop;
    if (kept > 0) {
        const auto* from = static_cast<const unsigned char*>(bytes) + static_cast<size_t>(drop) * frameBytes;
        ring_->write(from, static_cast<size_t>(kept), encoding);
    }
    return kept;
}

int64_t FileFeed::endOf(int64_t frameIndex, int fps) const {
    if (clockRate_ <= 0 || fps <= 0 || frameIndex < 0) return 0;
    return (frameIndex + 1) * clockRate_ / fps;
}

bool FileFeed::needs(int64_t frameIndex, int fps) const {
    // Before the first chunk the rate is unknown, so any frame needs more.
    if (clockRate_ <= 0) return fps > 0 && frameIndex >= 0;
    return ring_->written() < endOf(frameIndex, fps);
}

int FileFeed::deliver(int64_t frameIndex, int fps, Sink& sink) {
    const int64_t end = endOf(frameIndex, fps);
    int total = 0;
    while (cursor_->position() < end) {
        const int want = static_cast<int>(std::min<int64_t>(end - cursor_->position(), kReadFrames));
        const Cursor::Block block = cursor_->read(stereo_.data(), want);
        dropped_ += block.dropped;
        if (block.frames <= 0) break;   // the file has ended: the last frames get what there is
        sink.audio(stereo_.data(), block.frames, block.sampleRate);
        total += block.frames;
    }
    return total;
}

}  // namespace geode::audio::ingest
