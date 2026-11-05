#include "output/RenderPlan.hpp"

#include <algorithm>

namespace geode::output {

namespace {

// The earliest clip on `lane` that passes `ok`; ties keep the first in array order, as Kotlin's minByOrNull
// did over a lane's start-sorted clips. -1 for none.
template <typename Ok>
int earliestOn(const PlanClip* clips, int count, int lane, Ok ok) {
    int best = -1;
    for (int i = 0; i < count; ++i) {
        const PlanClip& c = clips[i];
        if (c.lane != lane || !ok(c)) continue;
        if (best < 0 || c.startMs < clips[best].startMs) best = i;
    }
    return best;
}

// The lowest lane index among the clips that pass `ok`, -1 for none.
template <typename Ok>
int firstLane(const PlanClip* clips, int count, Ok ok) {
    int lane = -1;
    for (int i = 0; i < count; ++i) {
        if (ok(clips[i]) && (lane < 0 || clips[i].lane < lane)) lane = clips[i].lane;
    }
    return lane;
}

}  // namespace

bool planRender(const PlanClip* visuals, int visualCount, const PlanClip* audio, int audioCount,
                int64_t minDurationMs, RenderPlan* out) {
    if (!visuals || !audio || visualCount <= 0 || audioCount <= 0) return false;
    auto scene = [](const PlanClip& c) {
        return c.has(PlanClip::kLaneLive) && c.has(PlanClip::kEnabled) && c.has(PlanClip::kWanted);
    };
    // A visual lane counts once it holds an enabled scene; an audio lane once it holds any enabled clip.
    const int visualLane = firstLane(visuals, visualCount, scene);
    if (visualLane < 0) return false;
    const int v = earliestOn(visuals, visualCount, visualLane, scene);
    const PlanClip& clip = visuals[v];
    const int songLane = firstLane(audio, audioCount, [](const PlanClip& c) {
        return c.has(PlanClip::kLaneLive) && c.has(PlanClip::kEnabled);
    });
    if (songLane < 0) return false;
    const int s = earliestOn(audio, audioCount, songLane, [&clip](const PlanClip& c) {
        return c.has(PlanClip::kEnabled) && c.has(PlanClip::kWanted) && c.overlaps(clip);
    });
    if (s < 0) return false;
    const PlanClip& song = audio[s];
    const int64_t start = std::max(clip.startMs, song.startMs);
    const int64_t end = std::min(clip.endMs, song.endMs);
    const int64_t duration = end - start;
    if (duration < minDurationMs) return false;
    const int64_t songStart = std::max<int64_t>(song.sourceInMs + (start - song.startMs), 0);
    const bool bounded = song.sourceDurationMs > 0;
    const bool whole = songStart == 0 && start == song.startMs && end == song.endMs && bounded &&
                       song.endMs - song.startMs >= song.sourceDurationMs;
    if (out) *out = RenderPlan{v, s, start, duration, songStart, whole};
    return true;
}

}  // namespace geode::output
