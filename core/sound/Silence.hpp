#pragma once

#include <cmath>
#include <cstdint>

// Whether a capture (the mic, another app's audio) is hearing only digital zero: what Android hands a
// capture an app refuses, so the Player can say so. Any sample above kCapturePeak is sound;
// kCaptureQuietMs of none since the capture started or last heard sound is quiet. The caller keeps the
// one timestamp, so there is no state here to own or free.
namespace geode::sound::silence {

inline constexpr float kCapturePeak = 1e-6f;
inline constexpr int64_t kCaptureQuietMs = 4000;

// The time the capture last heard sound, given the peak of the block it just wrote.
inline int64_t captureHeardAt(float peak, int64_t nowMs, int64_t heardAtMs) {
    return std::fabs(peak) > kCapturePeak ? nowMs : heardAtMs;
}

inline bool captureQuiet(int64_t nowMs, int64_t heardAtMs) { return nowMs - heardAtMs > kCaptureQuietMs; }

}  // namespace geode::sound::silence
