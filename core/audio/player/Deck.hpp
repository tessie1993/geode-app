#pragma once
#include <atomic>
#include <cstdint>

#include "audio/player/SpscRing.hpp"

namespace geode::audio::player {

// One track's decoded stereo audio at the output rate. The engine thread fills the ring and owns the
// object; the audio thread drains it. A seek makes a fresh deck rather than rewinding this one.
struct Deck {
    static constexpr int kChannels = 2;

    Deck(size_t ringFrames, int64_t token, int64_t startFrame, int64_t durationFrames)
        : ring(ringFrames * kChannels), token(token), startFrame(startFrame), durationFrames(durationFrames) {}

    SpscRing<float> ring;
    const int64_t token;
    const int64_t startFrame;
    const int64_t durationFrames;   // 0 when the container did not say
    std::atomic<bool> endOfStream{false};
    std::atomic<int64_t> framesConsumed{0};

    int64_t position() const { return startFrame + framesConsumed.load(std::memory_order_acquire); }
};

}  // namespace geode::audio::player
