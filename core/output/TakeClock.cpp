#include "output/TakeClock.hpp"

#include <algorithm>
#include <cmath>

namespace geode::output {

namespace {

constexpr int64_t kNsPerUs = 1000;
constexpr int64_t kUsPerSecond = 1'000'000;
constexpr int64_t kNsPerSecond = 1'000'000'000;

int evenDown(int v) { return v & ~1; }
int align16Down(int v) { return v & ~15; }

}  // namespace

TakeSize takeSize(int screenW, int screenH, int attempt) {
    if (screenW <= 1 || screenH <= 1 || attempt < 0 || attempt >= kMaxAttempts) return {};
    // No encoder takes a side past kMaxSide, so a huge stage starts there rather than wasting attempts.
    double scale = std::min(1.0, static_cast<double>(kMaxSide) / std::max(screenW, screenH));
    for (int i = 0; i < attempt; ++i) scale *= 0.75;
    const int w = static_cast<int>(std::floor(screenW * scale));
    const int h = static_cast<int>(std::floor(screenH * scale));
    // The screen's own size keeps its exact shape (even is all AVC asks); a scaled-down retry is for an
    // encoder that already refused once, so it gets the 16-pixel macroblock grid that every encoder takes.
    TakeSize out = attempt == 0 && scale == 1.0 ? TakeSize{evenDown(w), evenDown(h)} : TakeSize{align16Down(w), align16Down(h)};
    if (out.width < 16 || out.height < 16) return {};
    return out;
}

float takeFps(float displayFps) {
    if (!std::isfinite(displayFps) || displayFps <= 0.0f) return 30.0f;
    return std::clamp(displayFps, 24.0f, 60.0f);
}

int takeBitrate(TakeSize size, float fps) {
    const double bits = static_cast<double>(size.width) * size.height * takeFps(fps) * 0.15;
    return static_cast<int>(std::clamp(bits, 2.0e6, 40.0e6));
}

TakeFramePicker::TakeFramePicker(int64_t startNs, float fps, int64_t capUs)
    : startNs_(startNs), minGapUs_(std::llround(600000.0 / takeFps(fps))), capUs_(std::max<int64_t>(capUs, 0)) {}

std::optional<int64_t> TakeFramePicker::admit(int64_t frameNs) {
    if (frameNs < startNs_) return std::nullopt;
    const int64_t us = (frameNs - startNs_) / kNsPerUs;
    if (capUs_ > 0 && us > capUs_) {
        reachedCap_.store(true, std::memory_order_relaxed);
        return std::nullopt;
    }
    if (lastUs_ >= 0 && us - lastUs_ < minGapUs_) return std::nullopt;
    lastUs_ = us;
    ++frames_;
    return us;
}

TakeAudio::TakeAudio(int64_t startNs, int64_t capUs)
    : startNs_(startNs), capFrames_(capUs > 0 ? capUs * kRate / kUsPerSecond : 0) {}

int64_t TakeAudio::endNsLocked() const { return startNs_ + framesIn_ * kNsPerSecond / kRate; }

void TakeAudio::padFramesLocked(int64_t frames) {
    if (frames <= 0) return;
    buffer_.insert(buffer_.end(), static_cast<size_t>(frames) * kChannels, int16_t{0});
    framesIn_ += frames;
}

void TakeAudio::write(const int16_t* pcm, int frames, int channels, int rateHz, int64_t wallNs) {
    if (!pcm || frames <= 0 || channels <= 0 || rateHz <= 0) return;
    // To stereo first: mono is doubled, more than two channels keep the front pair.
    stereo_.resize(static_cast<size_t>(frames) * kChannels);
    for (int f = 0; f < frames; ++f) {
        const int16_t* in = pcm + static_cast<size_t>(f) * channels;
        stereo_[static_cast<size_t>(f) * 2] = in[0];
        stereo_[static_cast<size_t>(f) * 2 + 1] = channels > 1 ? in[1] : in[0];
    }
    std::lock_guard<std::mutex> guard(lock_);
    if (!resampler_ || resamplerRate_ != rateHz) {
        resampler_ = std::make_unique<audio::PcmResampler>(rateHz, kRate, kChannels);
        resamplerRate_ = rateHz;
    }
    resampled_.resize(resampler_->maxOutFrames(static_cast<size_t>(frames)) * kChannels);
    const auto out = static_cast<int64_t>(resampler_->process(stereo_.data(), static_cast<size_t>(frames), resampled_.data()));
    // Where this block belongs: the first one, and the first after a silence fill, land at their own time;
    // a block after a long hole fills the hole; anything else follows on sample by sample.
    const int64_t behindNs = wallNs - endNsLocked();
    if (!anchored_ || lastWasPad_ || behindNs > kGapNs) {
        padFramesLocked(behindNs * kRate / kNsPerSecond);
        anchored_ = true;
    }
    buffer_.insert(buffer_.end(), resampled_.begin(), resampled_.begin() + out * kChannels);
    framesIn_ += out;
    lastWasPad_ = false;
}

int TakeAudio::read(int16_t* out, int maxFrames, int64_t nowNs) {
    if (!out || maxFrames <= 0) return 0;
    std::lock_guard<std::mutex> guard(lock_);
    // Nothing written for a while: keep the track running in silence, kGapNs behind now, so a block that
    // arrives late still finds its place ahead of the fill.
    const int64_t fillTo = nowNs - kGapNs;
    if (pendingLocked() == 0 && fillTo > endNsLocked()) {
        padFramesLocked((fillTo - endNsLocked()) * kRate / kNsPerSecond);
        anchored_ = true;
        lastWasPad_ = true;
    }
    int64_t due = std::min<int64_t>(pendingLocked(), maxFrames);
    if (capFrames_ > 0) due = std::min(due, capFrames_ - framesOut_);
    const int n = static_cast<int>(due);
    if (n <= 0) return 0;
    std::copy(buffer_.begin() + static_cast<std::ptrdiff_t>(readAt_),
              buffer_.begin() + static_cast<std::ptrdiff_t>(readAt_ + static_cast<size_t>(n) * kChannels), out);
    readAt_ += static_cast<size_t>(n) * kChannels;
    framesOut_ += n;
    // Compact once the read half is the larger, so a long take holds only what is waiting.
    if (readAt_ > buffer_.size() / 2) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(readAt_));
        readAt_ = 0;
    }
    return n;
}

void TakeAudio::padTo(int64_t endUs) {
    std::lock_guard<std::mutex> guard(lock_);
    const int64_t want = endUs * kRate / kUsPerSecond;
    if (want > framesIn_) padFramesLocked(want - framesIn_);
}

int64_t TakeAudio::nextReadPtsUs() const {
    std::lock_guard<std::mutex> guard(lock_);
    return framesOut_ * kUsPerSecond / kRate;
}

int64_t TakeAudio::pending() const {
    std::lock_guard<std::mutex> guard(lock_);
    return pendingLocked();
}

int64_t TakeAudio::pendingLocked() const { return static_cast<int64_t>(buffer_.size() - readAt_) / kChannels; }

}  // namespace geode::output
