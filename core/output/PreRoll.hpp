#pragma once
#include <algorithm>
#include <cstdint>

#include "sound/hop/AnalysisThread.hpp"
#include "sound/interp/FrameBlend.hpp"

namespace geode::output {

// An export's pre-roll (#999 F1, F2, F4): before frame 0 the render's sound engine hears the track, listening
// without drawing, so the export opens on a warm engine, as live does playing on into the range. Without it
// the engine heard its first sample on frame 0: presence rose from 0, so the picture's clock
// (sound::motionRate) eased in from a stop over the first half second of every export, segment and loop
// (where the seam dissolved the full-speed tail into that slow opening), and the tempo, key and form started
// cold mid-song.
//
// A range with at least kPreRollWarmMs of track before it hears up to kPreRollMs of what precedes it and
// runs on into the range unbroken, as the player would. A range at (or just after) the track's start has
// nothing to hear first, so it hears the track's opening, and then the song starts over: a track boundary,
// as live at a gapless change, so the musical memory (tempo, key, form) knows nothing of what it heard ahead
// and frame 0 shows no tempo the range has not played yet, while the levels and presence are warm.
inline constexpr int64_t kPreRollMs = 8000;      // the tempo's first estimate waits 3 s, the form's arc 4 s
inline constexpr int64_t kPreRollWarmMs = 2000;  // presence and the levels' warm-up settle within it

struct PreRoll {
    int64_t fromMs = 0;     // where in the track the pre-roll's decode starts
    int64_t heardMs = 0;    // how much the engine hears from there
    bool boundary = false;  // the song starts over at the range (the opening was heard ahead)
};

inline PreRoll preRollFor(int64_t rangeStartMs) {
    const int64_t start = std::max<int64_t>(0, rangeStartMs);
    const int64_t before = std::min(start, kPreRollMs);
    if (before >= kPreRollWarmMs) return PreRoll{start - before, before, false};
    return PreRoll{0, kPreRollMs, true};
}

// Ends the pre-roll on the export's analysis (inline, AnalysisThread::pump). Its triggers and accents were heard
// before the range, so the reader delivers none of them; the boundary, when the plan names one, starts the song over
// and is true here: the caller fires it on frame 0.
inline bool endPreRoll(sound::AnalysisThread& analysis, sound::interp::FrameReader& reader, int64_t head, const PreRoll& plan) {
    analysis.pump(0.0f);
    reader.heardThrough(analysis, head);
    if (!plan.boundary) return false;
    analysis.trackBoundary();
    analysis.pump(0.0f);
    return true;
}

}  // namespace geode::output
