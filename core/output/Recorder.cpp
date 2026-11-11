#include "output/Recorder.hpp"

#include <algorithm>
#include <cmath>

namespace geode::output::record {

namespace {

constexpr int64_t kNsPerSec = 1'000'000'000;
constexpr int64_t kNsPerMs = 1'000'000;
constexpr int64_t kUsPerSec = 1'000'000;
constexpr int64_t kMsPerSec = 1'000;

int64_t limitOf(const Config& c) {
    const int64_t maxNs = c.maxSec > 0 ? c.maxSec * kNsPerSec : 0;
    const int64_t capNs = c.capMs > 0 ? c.capMs * kNsPerMs : 0;
    if (maxNs == 0) return capNs;
    if (capNs == 0) return maxNs;
    return std::min(maxNs, capNs);
}

int64_t framesOf(int64_t ms) { return ms * Recorder::kAudioRateHz / kMsPerSec; }

int16_t toS16(float x) {
    if (!(x == x)) return 0;
    return static_cast<int16_t>(std::clamp(x, -1.0f, 1.0f) * 32767.0f);
}

}  // namespace

Recorder::Recorder(const Config& config)
    : config_{config.stageWidth, config.stageHeight, encode::clampFps(config.fps), config.maxSec, config.capMs},
      limitNs_(limitOf(config)) {
    hold_.reserve(static_cast<size_t>(framesOf(kHoldMaxMs) * kChannels));
}

int64_t Recorder::position(int64_t elapsedNs) const {
    if (elapsedNs <= 0) return 0;
    // Whole seconds apart from the rest, so hours of nanoseconds times the rate never overflow.
    const int64_t sec = elapsedNs / kNsPerSec;
    const int64_t rest = elapsedNs % kNsPerSec;
    return sec * kAudioRateHz + (rest * kAudioRateHz + kNsPerSec / 2) / kNsPerSec;
}

int64_t Recorder::frame(int64_t nowNs, int stageWidth, int stageHeight) {
    if (stopped() != Stop::None) return -1;
    if (stageWidth != config_.stageWidth || stageHeight != config_.stageHeight) {
        stop(Stop::Resized, nowNs);
        return -1;
    }
    if (!started()) {
        startNs_.store(nowNs, std::memory_order_release);
        started_.store(true, std::memory_order_release);
    }
    const int64_t elapsed = std::max<int64_t>(nowNs - startNs_.load(std::memory_order_relaxed), 0);
    if (limitNs_ > 0 && elapsed >= limitNs_) {
        stop(Stop::MaxLength, nowNs);
        return -1;
    }
    const int64_t slotNs = kNsPerSec / config_.fps;
    // The stage's own frame interval, smoothed: a frame up to half of it before a slot's start is that
    // slot's frame, so the line between "this slot" and "the next" falls between two stage frames.
    if (lastFrameNs_ >= 0) {
        const int64_t interval = std::clamp<int64_t>(nowNs - lastFrameNs_, 0, slotNs);
        stageFrameNs_ = stageFrameNs_ < 0 ? interval : stageFrameNs_ + (interval - stageFrameNs_) / kStageSmoothing;
    }
    lastFrameNs_ = nowNs;
    const int64_t tolerance = slotNs / kSlotToleranceDiv;
    const int64_t claim = stageFrameNs_ < 0 ? tolerance : std::max(stageFrameNs_ / 2, tolerance);
    const int64_t slot = (elapsed + claim) * config_.fps / kNsPerSec;
    if (slot <= lastSlot_) return -1;
    const int64_t pending = taken_.load(std::memory_order_relaxed) - encoded_.load(std::memory_order_relaxed);
    if (pending >= kMaxPending && nowNs - lastSubmitNs_ < kProbeNs) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    const int64_t grid = slot * kNsPerSec / config_.fps;
    if (limitNs_ > 0 && grid >= limitNs_) {
        stop(Stop::MaxLength, nowNs);
        return -1;
    }
    int64_t pts = std::llabs(elapsed - grid) <= tolerance ? grid : elapsed;
    if (pts <= lastPtsNs_) pts = lastPtsNs_ + 1;
    lastSlot_ = slot;
    lastPtsNs_ = pts;
    lastSubmitNs_ = nowNs;
    taken_.fetch_add(1, std::memory_order_relaxed);
    return pts;
}

void Recorder::restarted() {
    std::lock_guard<std::mutex> lock(audioLock_);
    exact_ = true;
    resampler_.reset();
}

void Recorder::wrote(int64_t bytes, int64_t nowNs) {
    if (bytes <= 0) return;
    if (bytes_.fetch_add(bytes, std::memory_order_relaxed) + bytes >= kFileLimitBytes) stop(Stop::FileFull, nowNs);
}

void Recorder::stop(Stop why, int64_t nowNs) {
    std::lock_guard<std::mutex> lock(audioLock_);
    stopLocked(why, nowNs);
}

void Recorder::stopLocked(Stop why, int64_t nowNs) {
    if (why == Stop::None || stopped() != Stop::None) return;
    int64_t at = nowNs;
    if (started()) {
        const int64_t start = startNs_.load(std::memory_order_relaxed);
        at = std::max(at, start);
        if (limitNs_ > 0) at = why == Stop::MaxLength ? start + limitNs_ : std::min(at, start + limitNs_);
    }
    stopNs_.store(at, std::memory_order_release);
    stop_.store(static_cast<int>(why), std::memory_order_release);
}

int64_t Recorder::recordedMs(int64_t nowNs) const {
    if (!started()) return 0;
    const int64_t end = stopped() != Stop::None ? stopNs_.load(std::memory_order_acquire) : nowNs;
    int64_t elapsed = std::max<int64_t>(end - startNs_.load(std::memory_order_relaxed), 0);
    if (limitNs_ > 0) elapsed = std::min(elapsed, limitNs_);
    return elapsed / kNsPerMs;
}

int64_t Recorder::audioFrames() const {
    std::lock_guard<std::mutex> lock(audioLock_);
    return emitted_;
}

int64_t Recorder::eatPad(int64_t frames) {
    const int64_t from = std::max(padStart_, emitted_);
    const int64_t eat = std::min(frames, padEnd_ - from);
    if (eat <= 0) return 0;
    const auto first = hold_.begin() + static_cast<std::ptrdiff_t>((from - emitted_) * kChannels);
    hold_.erase(first, first + static_cast<std::ptrdiff_t>(eat * kChannels));
    padEnd_ -= eat;
    return eat;
}

void Recorder::fillGap(int64_t frames) {
    if (frames <= 0) return;
    const int64_t head = emitted_ + held();
    // Silence that follows silence is one stretch, so a burst can be laid back over all of it.
    if (padEnd_ != head || padEnd_ <= std::max(padStart_, emitted_)) padStart_ = head;
    hold_.insert(hold_.end(), static_cast<size_t>(frames * kChannels), 0);
    padEnd_ = head + frames;
}

// Lays block_ (frames long) into the hold so that it ends where the picture is.
void Recorder::place(int64_t frames, int64_t nowNs) {
    const int64_t start = startNs_.load(std::memory_order_relaxed);
    int64_t elapsed = nowNs - start;
    if (limitNs_ > 0) elapsed = std::min(elapsed, limitNs_);
    const int64_t target = position(elapsed);
    const int64_t tolerance = framesOf(kSyncToleranceMs);
    int64_t skip = 0;
    const int64_t error = emitted_ + held() + frames - target;   // > 0: the sound is behind the picture
    if (error > 0) {
        // Silence still waiting gives way first: a burst after a pause is laid back over it, losing nothing.
        const int64_t late = error - eatPad(error);
        if (late > (exact_ ? 0 : tolerance)) skip = std::min(late, frames);
        earlySinceNs_ = -1;
    } else if (error < 0 && exact_) {
        fillGap(-error);
        earlySinceNs_ = -1;
    } else if (error < -tolerance) {
        // A block seen late (a slow frame) reads early once; only one that stays early is a real gap, and
        // the least early reading in that time is how large it is.
        if (earlySinceNs_ < 0) {
            earlySinceNs_ = nowNs;
            earlyLeast_ = error;
        }
        earlyLeast_ = std::max(earlyLeast_, error);
        if (nowNs - earlySinceNs_ >= kEarlyHoldNs) {
            fillGap(-earlyLeast_);
            earlySinceNs_ = -1;
        }
    } else {
        earlySinceNs_ = -1;
    }
    const int64_t room = framesOf(kHoldMaxMs) - held();
    const int64_t keep = std::min(frames - skip, std::max<int64_t>(room, 0));
    skip = frames - keep;
    if (keep <= 0) return;
    const int16_t* from = block_.data() + skip * kChannels;
    hold_.insert(hold_.end(), from, from + keep * kChannels);
    exact_ = false;
}

void Recorder::hear(const float* interleaved, int frames, int channels, int rateHz, int64_t nowNs) {
    if (!interleaved || frames <= 0 || channels <= 0 || rateHz <= 0) return;
    if (!started() || stopped() != Stop::None) return;
    std::lock_guard<std::mutex> lock(audioLock_);
    source_.resize(static_cast<size_t>(frames) * kChannels);
    for (int i = 0; i < frames; ++i) {
        const float* in = interleaved + static_cast<size_t>(i) * channels;
        source_[static_cast<size_t>(i) * 2] = toS16(in[0]);
        source_[static_cast<size_t>(i) * 2 + 1] = toS16(channels > 1 ? in[1] : in[0]);
    }
    if (!resampler_ || resamplerRateHz_ != rateHz) {
        resampler_ = std::make_unique<audio::PcmResampler>(rateHz, kAudioRateHz, kChannels);
        resamplerRateHz_ = rateHz;
    }
    block_.resize(resampler_->maxOutFrames(static_cast<size_t>(frames)) * kChannels);
    const int64_t made = static_cast<int64_t>(resampler_->process(source_.data(), static_cast<size_t>(frames), block_.data()));
    if (made > 0) place(made, nowNs);
}

int Recorder::takeAudio(int64_t nowNs, int16_t* out, int capacityFrames, int64_t* ptsUs) {
    if (!out || capacityFrames <= 0 || !started()) return 0;
    std::lock_guard<std::mutex> lock(audioLock_);
    const int64_t start = startNs_.load(std::memory_order_relaxed);
    if (stopped() == Stop::None && limitNs_ > 0 && nowNs - start >= limitNs_) stopLocked(Stop::MaxLength, nowNs);
    const bool ended = stopped() != Stop::None;
    const int64_t until = ended ? stopNs_.load(std::memory_order_relaxed) - start : nowNs - start - kLookbackMs * kNsPerMs;
    const int64_t due = position(until) - emitted_;
    if (due <= 0) return 0;
    const int64_t count = std::min<int64_t>(due, capacityFrames);
    const int64_t fromHold = std::min(count, held());
    std::copy(hold_.begin(), hold_.begin() + static_cast<std::ptrdiff_t>(fromHold * kChannels), out);
    hold_.erase(hold_.begin(), hold_.begin() + static_cast<std::ptrdiff_t>(fromHold * kChannels));
    if (fromHold < count) {
        std::fill(out + fromHold * kChannels, out + count * kChannels, static_cast<int16_t>(0));
        // Silence went out where sound would have been: whatever comes next starts from the picture again.
        exact_ = true;
    }
    if (ptsUs) *ptsUs = emitted_ / kAudioRateHz * kUsPerSec + emitted_ % kAudioRateHz * kUsPerSec / kAudioRateHz;
    emitted_ += count;
    return static_cast<int>(count);
}

encode::FrameSize frameSize(int stageWidth, int stageHeight, int shortSide) {
    if (stageWidth <= 0 || stageHeight <= 0 || shortSide <= 0) return {};
    return encode::frameSize(std::min(shortSide, std::min(stageWidth, stageHeight)), stageWidth, stageHeight);
}

int videoBps(int width, int height, int fps, int64_t limitMs) {
    if (width <= 0 || height <= 0) return 0;
    constexpr int64_t kReferenceBps = 12'000'000;
    constexpr int64_t kReferencePixels = 1920LL * 1080;
    constexpr int64_t kCeilingBps = 40'000'000;
    int64_t bps = kReferenceBps * width * height / kReferencePixels;
    if (fps > 30) bps += bps / 2;
    const int asked = static_cast<int>(std::clamp<int64_t>(bps, encode::kFloorBps, kCeilingBps));
    if (limitMs <= 0) return asked;
    const int64_t durationUs = limitMs * kMsPerSec;
    return encode::cappedVideoBps(asked, durationUs, encode::audioBytes(durationUs));
}

}  // namespace geode::output::record
