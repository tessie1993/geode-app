#pragma once

#include <cstdint>

#include "signal/events/Event.hpp"
#include "sound/SoundFrame.h"

// Musical events from the sound frame: one detector per renderer, fed every frame in order, reset when
// the sound engine is. It decides nothing about the music; core/sound has already heard it. It only
// turns the frame's values into events, so there is one definition of a beat, a key or a drop.
//
// Three ways an event is read, one per row of SoundEvents.cpp's table:
//
//  - Counter-driven: Beat (beatCount) and Downbeat (barCount). A trigger field holds the strongest hit
//    since the last frame, so two beats that land between drawn frames (a slow frame, a GL hitch, a low
//    export fps against a fast tempo) read as one. The frame's own counter says how many there were: the
//    detector diffs it against the last value it saw and raises one event per step, at most
//    kMaxBurst. A counter that goes backwards (an engine reset, a new epoch) is a resync: nothing is
//    raised and the new value is taken as the base. The 4096 wrap is a step forward, not a resync. The
//    trigger still gates the count: Rhythm ticks its counters while it is unlocked too, but only a
//    locked beat is an event, and only the trigger knows (> 0 iff at least one locked beat ticked).
//  - Trigger-driven: every other sound-frame trigger (> 0 on the one frame it fires, the value its
//    strength). Their kinds have no counter in the frame, so two between frames still merge. Phrase is
//    one of them: phraseIndex exists but also steps when a section restarts the phrase count with no
//    phrase trigger, so its steps are not phrase events.
//  - Derived from how the frame changes: BuildStart and BuildEnd (sectionState), KeyChange (the key
//    the frame reads, keyOf), SectionChange (the sectionChange trigger, carrying sectionId).

namespace geode::signal {

class SoundEvents {
public:
    // The most events one counter raises in one frame; past it a stall's beats are dropped, not replayed.
    static constexpr int kMaxBurst = 8;

    // Raises this frame's events into `out`, in table order then derived order, so a frame always
    // lists its events the same way. dt is unused today; it stays for derived rules that need time.
    void detect(const GeodeSoundFrame& frame, double timeS, float dt, EventQueue& out);
    void reset();

private:
    // Per counter row: the last value seen, -1 before any frame (the first frame seeds it).
    static constexpr int kCounterRows = 2;
    std::int64_t lastCount_[kCounterRows] = {-1, -1};
    int sectionState_ = -1;  // -1: no frame seen yet
    int key_ = -1;           // the key last read (keyOf), -1 before any
};

// The key the frame reads, as core/sound/Harmony holds it (its own two-second hold already applied):
// 0..11 major on its tonic pitch class, 12..23 minor. Decoded from keyRoot (the tonic) and keyHue
// (the tonic's place on the circle of fifths for a major key, its relative major's for a minor one),
// not from keyMajor, which is a smoothed lean rather than the held key's mode. -1 when the two do not
// name a key (non-finite, or inconsistent).
int keyOf(const GeodeSoundFrame& frame);

}  // namespace geode::signal
