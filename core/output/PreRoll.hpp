#pragma once
#include <algorithm>
#include <cstdint>

#include "sound/SoundEngine.hpp"

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

// Ends the pre-roll on the export's engine. Its triggers and accent were heard before the range, so a step
// drops them rather than fire them on frame 0; the boundary, when the plan names one, lands on frame 0.
// False when there is no engine, i.e. the track gave the engine nothing to hear: the shell fails the render
// rather than export a picture frozen by the silence rule (F1). A track that decodes to silence is heard,
// and stays still like any silence.
inline bool endPreRoll(sound::SoundEngine* engine, const PreRoll& plan) {
    if (engine == nullptr) return false;
    GeodeSoundFrame dropped{};
    engine->step(0.0f, dropped);
    if (plan.boundary) engine->trackBoundary();
    return true;
}

}  // namespace geode::output
