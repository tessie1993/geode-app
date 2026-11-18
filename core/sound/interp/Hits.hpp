#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "sound/SoundFrame.h"

namespace geode::sound::interp {

// Where each trigger of a read frame fired (FrameReader::read): the ring position of the hop that fired it, so a
// reader knows its sample offset from the frame's position. Indexed by the scalar's place in GeodeSoundFrame;
// -1 for a trigger that did not fire this frame.
struct Hits {
    std::array<int64_t, GEODE_SOUND_SCALARS> at;

    Hits() { clear(); }
    void clear() { at.fill(-1); }
    // The trigger at byte `offset` in GeodeSoundFrame.
    int64_t atOffset(size_t offset) const {
        const size_t i = offset / sizeof(float);
        return i < at.size() ? at[i] : -1;
    }
};

}  // namespace geode::sound::interp
