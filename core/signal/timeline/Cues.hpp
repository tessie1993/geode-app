#pragma once
#include <cstdint>
#include <vector>

namespace geode::signal::timeline {

// Captions over one rendered item (#422; ported from the Kotlin TimedTextOverlay.forSpan and getText).
//
// A project's text-lane clips are cues on project time. Each picture item of a Studio composition takes the
// cues touching its span [spanStart, spanEnd), re-based so the item's first frame is 0 and cut to the
// item's length. Media3 then asks, on Transformer's GL thread, once per frame, what the caption is; the
// first frame's timestamp is the item's origin, since the timestamps Media3 hands an overlay are not
// promised to start at 0. The text itself stays Kotlin, keyed by each cue's index in the list given.
//
// Nothing here outlives a call across the ABI: the item's cues leave as (start, end, source) triples and its
// origin as a CueClock, both kept by the caller, because Media3 does not promise to release an overlay exactly
// once or to ask for no frame after it (#422). The lookup is allocation-free.
class CueTable {
public:
    struct Cue {
        int64_t startMs;
        int64_t endMs;
        int32_t source;  // index into the list the cues came from
    };

    // The cues of (starts[i], ends[i]) that touch the span, re-based; in their given order, so the first
    // that spans a time wins, as it did in Kotlin.
    static CueTable forSpan(const int64_t* startsMs, const int64_t* endsMs, int count, int64_t spanStartMs,
                            int64_t spanEndMs);

    bool empty() const { return cues_.empty(); }
    const std::vector<Cue>& cues() const { return cues_; }

    // The source index of the cue on screen at item time atMs, or -1 for none.
    int32_t at(int64_t atMs) const;

    // The same over cues laid out as (startMs, endMs, source) triples, as the ABI hands them back.
    static int32_t atTriples(const int64_t* triples, int count, int64_t atMs);

private:
    std::vector<Cue> cues_;
};

// An item's caption clock: the first frame's presentation time is its origin. Plain state the caller keeps
// (two int64s, the ABI's origin pair); all zeros is an item that has not drawn a frame yet.
struct CueClock {
    int64_t hasOrigin = 0;
    int64_t originUs = 0;

    // Item time in ms for a frame; the first call fixes the origin.
    int64_t itemMs(int64_t presentationUs);
};

}  // namespace geode::signal::timeline
