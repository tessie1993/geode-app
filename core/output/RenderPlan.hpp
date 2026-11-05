#pragma once
#include <cstdint>

namespace geode::output {

// What a project render draws, as times (#657; ported from the Kotlin ProjectRenderPlan.plan, #636). Every
// render is one visual over one song: the first enabled scene clip on the first live visual lane, over the
// earliest enabled song on the first live audio lane that it overlaps. Kotlin keeps reading the project
// document (it is app data) and hands each clip over as a PlanClip; this picks, cuts the two against each
// other, maps programme time to song time and says whether the stretch is the whole song. Pure, any thread,
// host-tested by core/tests/output_test.

// One clip on the timeline, on programme time. `lane` is its lane's index in the timeline: lane order is what
// "first" means, so a lower index wins whatever order the clips arrive in.
struct PlanClip {
    int64_t startMs = 0;
    int64_t endMs = 0;
    // Offset into the media and how much of it there is; sourceDurationMs 0 is an unbounded source (a scene,
    // or a song whose length the project does not know).
    int64_t sourceInMs = 0;
    int64_t sourceDurationMs = 0;
    int lane = 0;
    int flags = 0;

    // The clip plays (not switched off).
    static constexpr int kEnabled = 1;
    // It holds what its side wants: a scene on a visual lane, a song on an audio lane.
    static constexpr int kWanted = 2;
    // Its lane plays (not muted or hidden).
    static constexpr int kLaneLive = 4;

    bool has(int flag) const { return (flags & flag) != 0; }
    bool overlaps(const PlanClip& o) const { return startMs < o.endMs && o.startMs < endMs; }
};

struct RenderPlan {
    // Indices into the visual and audio arrays.
    int visual = -1;
    int song = -1;
    int64_t programmeStartMs = 0;
    int64_t durationMs = 0;
    int64_t songStartMs = 0;
    // The stretch is the whole song from its start, so the exporter reads the song's own length (which a
    // project may not know) instead of being handed a range.
    bool wholeSong = false;
};

// False (out untouched) when there is no visual, no live audio lane with an enabled clip, no song on that
// lane overlapping the visual, or less than minDurationMs of the two together. minDurationMs is the editor's
// MIN_CLIP_DURATION_MS, passed in so the editor stays its one owner. Only the first live audio lane is tried:
// when it has no song under the visual there is no plan, as in the Kotlin.
bool planRender(const PlanClip* visuals, int visualCount, const PlanClip* audio, int audioCount,
                int64_t minDurationMs, RenderPlan* out);

}  // namespace geode::output
