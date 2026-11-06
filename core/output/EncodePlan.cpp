#include "output/EncodePlan.hpp"

#include <algorithm>
#include <climits>

namespace geode::output::encode {

namespace {
constexpr int64_t kUsPerSecond = 1'000'000;
constexpr int64_t kBitsPerByte = 8;
constexpr int kFallbackCeilingFps = 30;

int even(int v) { return v % 2 == 0 ? v : v + 1; }
}  // namespace

int cappedVideoBps(int requestedBps, int64_t durationUs, int64_t audioBytesIn, int64_t limitBytes) {
    if (durationUs <= 0 || requestedBps <= 0) return requestedBps;
    const int64_t videoBytes = std::max<int64_t>(0, limitBytes - std::max<int64_t>(0, audioBytesIn));
    const int64_t fitting = videoBytes * kBitsPerByte * kUsPerSecond / durationUs;
    if (fitting >= requestedBps) return requestedBps;
    return std::max(static_cast<int>(fitting), std::min(kFloorBps, requestedBps));
}

int64_t audioBytes(int64_t durationUs, int bps) {
    return std::max<int64_t>(0, durationUs) * bps / (kBitsPerByte * kUsPerSecond);
}

FrameSize frameSize(int shortSide, int wRatio, int hRatio) {
    if (shortSide <= 0 || wRatio <= 0 || hRatio <= 0) return {};
    const bool landscape = wRatio >= hRatio;
    int longSide = static_cast<int>(static_cast<int64_t>(shortSide) * std::max(wRatio, hRatio) / std::min(wRatio, hRatio));
    int shortOut = shortSide;
    if (longSide > kMaxAvcDim) {
        shortOut = static_cast<int>(static_cast<int64_t>(shortSide) * kMaxAvcDim / longSide);
        longSide = kMaxAvcDim;
    }
    return landscape ? FrameSize{even(longSide), even(shortOut)} : FrameSize{even(shortOut), even(longSide)};
}

int fallbackFps(int requestedFps) { return requestedFps > kFallbackCeilingFps ? requestedFps / 2 : requestedFps; }

int clampFps(int requestedFps) { return std::clamp(requestedFps, kMinFps, kMaxFps); }

}  // namespace geode::output::encode
