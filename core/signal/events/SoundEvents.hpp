#pragma once

#include "signal/events/Event.hpp"
#include "sound/SoundFrame.h"

// Musical events from the sound frame. Most are the frame's own triggers (> 0 on the one frame they
// fire, the value their strength); a few are derived here from how the frame changes, which needs
// memory across frames, so this is a small stateful detector: one per renderer, fed every frame in
// order, reset when the sound engine is.

namespace geode::signal {

class SoundEvents {
public:
    // A key has to read this clear, for this long, before it counts as a change of key.
    static constexpr float kKeyClarity = 0.35f;
    static constexpr float kKeyHoldSeconds = 1.0f;

    // Raises this frame's events into `out`, in table order then derived order, so a frame always
    // lists its events the same way.
    void detect(const GeodeSoundFrame& frame, double timeS, float dt, EventQueue& out);
    void reset();

private:
    int sectionState_ = -1;  // -1: no frame seen yet
    int key_ = -1;           // the key last raised, -1 none
    int candidateKey_ = -1;  // a different clear key, waiting out kKeyHoldSeconds
    float candidateSeconds_ = 0.0f;
};

// The key a frame reads: 0..11 major on its tonic pitch class, 12..23 minor; -1 when it is not clear.
int keyOf(const GeodeSoundFrame& frame, float minClarity);

}  // namespace geode::signal
