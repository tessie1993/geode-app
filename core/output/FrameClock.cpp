#include "output/FrameClock.hpp"

namespace geode::output {

namespace {
constexpr int64_t kUsPerSecond = 1'000'000;
constexpr int64_t kNsPerSecond = 1'000'000'000;
constexpr int64_t kMsPerSecond = 1'000;

int64_t nonNegative(int64_t v) { return v < 0 ? 0 : v; }
}  // namespace

FrameClock::FrameClock(int fps, int sampleRateHz) : fps_(fps > 0 ? fps : 1), rate_(sampleRateHz > 0 ? sampleRateHz : 0) {}

int64_t FrameClock::framesFor(int64_t durationUs) const {
    const int64_t frames = nonNegative(durationUs) * fps_ / kUsPerSecond;
    return frames < 1 ? 1 : frames;
}

int64_t FrameClock::ptsNs(int64_t n) const {
    // n stays far below 2^63 / 1e9 (292 years at any frame rate), so the product cannot overflow.
    return nonNegative(n) * kNsPerSecond / fps_;
}

int64_t FrameClock::frameStartUs(int64_t n) const { return nonNegative(n) * kUsPerSecond / fps_; }

int64_t FrameClock::frameTimeMs(int64_t n) const { return nonNegative(n) * kMsPerSecond / fps_; }

int64_t FrameClock::samplesThrough(int64_t n) const {
    // llround(x / fps) for non-negative x is floor((2x + fps) / (2 fps)), exact in integers.
    const int64_t x = (nonNegative(n) + 1) * rate_;
    return (2 * x + fps_) / (2 * static_cast<int64_t>(fps_));
}

}  // namespace geode::output
