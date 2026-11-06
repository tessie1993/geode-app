#pragma once

// The loop render's seam driver (#603): which source frames a loop stashes and which it presents. Pure
// logic, no GL, so core/tests/loop_seam_test.cpp pins it on the host; geode_export_seam_frame
// (core/api/geode_export_pass_api.cpp) runs it beside the SeamPass of ExportPasses.hpp. The presentation
// time comes from core/output/FrameClock and the stash budget from core/output/LoopPlan (LoopSpec::budgeted).
// The loop render's report (#752): progress and reportedCrossfadeMs, geode_export_loop_progress and
// geode_export_loop_reported_crossfade_ms.
//
// The schedule. A stop renders loopFrames + seamFrames source frames. The opening seamFrames are drawn but
// not presented: they are copied into the stash and laid back over the closing seamFrames at the dissolve's
// weight (ExportPassMath.hpp, exportmath::seam::weight), so the loop's last frame dissolves into what the
// viewer is about to see again. Output frame j is source frame j + seamFrames, which keeps the presented
// timestamps monotonic from zero and the encoder in a single pass.

#include <cstdint>

namespace geode::viz::frame::loopseam {

enum class Op { Capture, Present, BlendPresent };

// What source frame `frame` of a stop does. index is the stash frame it captures into or blends from;
// tail is the closing frame it is (the blend's position in the dissolve); both are 0 when unused.
struct Step {
    Op op = Op::Present;
    int index = 0;
    int tail = 0;
};

inline Step step(int frame, int loopFrames, int seamFrames) {
    if (frame < seamFrames) return {Op::Capture, frame, 0};
    const int tail = frame - loopFrames;
    if (tail >= 0) return {Op::BlendPresent, tail, tail};
    return {};
}

// The crossfade a finished reel reports: seamFrames at fps in whole milliseconds, truncated (Kotlin's
// `seamFrames * 1000L / fps`, which renderStops used to work out). seamFrames is the shortest seam any stop
// of the reel kept, so when the stash budget cut the seam (LoopSpec::budgeted) this is the budgeted figure. No
// frame rate reads as one frame a second.
inline int64_t reportedCrossfadeMs(int seamFrames, int fps) {
    return static_cast<int64_t>(seamFrames) * 1000LL / (fps > 0 ? fps : 1);
}

// The fraction of a whole loop render done once source frame `frame` of stop `stop` (of `stops`) is drawn,
// each stop a 1/stops share of loopFrames + seamFrames source frames. Worked in double from whole counts, so
// it rises with every frame (a later stop's first frame is past an earlier stop's last, even when a later
// stop kept fewer seam frames), and the last frame of the last stop lands on exactly 1. Clamped to 0..1, so a
// caller's stray index never reports past the ends.
inline float progress(int stop, int stops, int frame, int loopFrames, int seamFrames) {
    if (stops <= 0) return 1.0f;
    const int sourceFrames = loopFrames + seamFrames;
    const double within = sourceFrames > 0 ? static_cast<double>(frame + 1) / sourceFrames : 1.0;
    const double done = (static_cast<double>(stop) + within) / stops;
    if (!(done > 0.0)) return 0.0f;
    if (done >= 1.0) return 1.0f;
    return static_cast<float>(done);
}

}  // namespace geode::viz::frame::loopseam
