#pragma once
#include <cstdint>

// Buffer sizes from durations. A ring is sized by how long it must hold sound at the fastest rate it can be
// fed, never by a frame count picked by hand; a ring's index is masked, so the count is rounded up to a power
// of two. Pure, constexpr: every size here is fixed at compile time and held by a static_assert where it is used.
namespace geode::sound::budget {

// The fastest stream the player hands over, and the rate a capture (the mic, another app, a decode) runs at.
inline constexpr int kMaxRateHz = 192'000;
inline constexpr int kCaptureRateHz = 48'000;

constexpr int64_t framesIn(int milliseconds, int rateHz) {
    return (static_cast<int64_t>(milliseconds) * rateHz + 999) / 1000;
}

constexpr int roundUpToPowerOfTwo(int64_t frames) {
    int size = 1;
    while (size < frames) size <<= 1;
    return size;
}

// The smallest power-of-two ring that holds `milliseconds` of sound at `rateHz`, plus `extraFrames`.
constexpr int ringFrames(int milliseconds, int rateHz, int extraFrames = 0) {
    return roundUpToPowerOfTwo(framesIn(milliseconds, rateHz) + extraFrames);
}

}  // namespace geode::sound::budget
