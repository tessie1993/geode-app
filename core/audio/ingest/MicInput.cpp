#include "audio/ingest/MicInput.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace geode::audio::ingest {

void ReopenBudget::reset() {
    std::fill(std::begin(at_), std::end(at_), kNever);
    next_ = 0;
}

bool ReopenBudget::take(int64_t nowMs) {
    const auto inWindow = [nowMs](int64_t at) { return at != kNever && nowMs - at < kWindowMs; };
    const auto recent = std::count_if(std::begin(at_), std::end(at_), inWindow);
    if (recent >= kMax) return false;
    at_[next_] = nowMs;
    next_ = (next_ + 1) % kMax;
    return true;
}

bool MicInput::start(std::shared_ptr<PcmRing> ring, int deviceRateHz, bool unprocessed) {
    std::lock_guard<std::mutex> lock(lock_);
    if (stream_) return true;
    if (!ring) return false;
    ring_ = std::move(ring);
    deviceRateHz_ = deviceRateHz;
    unprocessed_ = unprocessed;
    reopens_.reset();
    writing_.store(ring_.get(), std::memory_order_release);
    if (!openLocked()) {
        endLocked();
        return false;
    }
    running_.store(true, std::memory_order_release);
    return true;
}

void MicInput::stop() {
    std::unique_ptr<MicStream> stream;
    std::shared_ptr<PcmRing> ring;
    {
        std::lock_guard<std::mutex> lock(lock_);
        running_.store(false, std::memory_order_release);
        writing_.store(nullptr, std::memory_order_release);
        stream = std::move(stream_);
        ring = std::move(ring_);
    }
    // Closed outside the lock, since the platform's error thread may wait for it in onClosed; and before the
    // ring goes, since a callback already past writing_ may still be writing it.
    stream.reset();
}

bool MicInput::onAudio(const void* data, int frames) {
    PcmRing* ring = writing_.load(std::memory_order_acquire);
    if (!ring) return false;
    if (!data || frames <= 0) return true;
    const int channels = channels_.load(std::memory_order_relaxed);
    const int rate = rateHz_.load(std::memory_order_relaxed);
    if (asFloat_.load(std::memory_order_relaxed)) {
        ring->writeFloat(static_cast<const float*>(data), frames, channels, rate);
    } else {
        ring->writeInt16(static_cast<const int16_t*>(data), frames, channels, rate);
    }
    return true;
}

void MicInput::onClosed(const void* id, bool disconnected, int64_t nowMs) {
    // Declared before the lock, so the closed stream is let go of after the lock is.
    std::unique_ptr<MicStream> closed;
    std::lock_guard<std::mutex> lock(lock_);
    // Only the stream running now: one that stop or a reopen already replaced reports late.
    if (!stream_ || stream_->id() != id) return;
    closed = std::move(stream_);
    if (disconnected && reopens_.take(nowMs) && openLocked()) return;
    endLocked();
}

bool MicInput::openLocked() {
    CaptureFormat plan[kMaxCaptureFormats];
    const int count = std::min(capturePlan(deviceRateHz_, false, plan, kMaxCaptureFormats), kMaxCaptureFormats);
    for (int i = 0; i < count; ++i) {
        std::unique_ptr<MicStream> stream = streams_.open(MicRequest{plan[i], unprocessed_, kCapacityFrames}, *this);
        if (!stream) continue;
        rateHz_.store(plan[i].rateHz, std::memory_order_relaxed);
        channels_.store(plan[i].channels, std::memory_order_relaxed);
        asFloat_.store(plan[i].asFloat, std::memory_order_relaxed);
        // A stream the device opened but will not start ends the try, as a refused startRecording did.
        if (!stream->start()) return false;
        stream_ = std::move(stream);
        return true;
    }
    return false;
}

void MicInput::endLocked() {
    running_.store(false, std::memory_order_release);
    writing_.store(nullptr, std::memory_order_release);
    ring_.reset();
}

}  // namespace geode::audio::ingest
