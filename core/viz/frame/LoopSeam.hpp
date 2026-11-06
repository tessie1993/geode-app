#pragma once

// The loop render's seam driver (#603): which source frames a loop stashes and which it presents. Pure
// logic, no GL, so core/tests/loop_seam_test.cpp pins it on the host; geode_export_seam_frame
// (core/api/geode_export_pass_api.cpp) runs it beside the SeamPass of ExportPasses.hpp. The presentation
// time comes from core/output/FrameClock and the stash budget from core/output/LoopPlan (LoopSpec::budgeted).
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

}  // namespace geode::viz::frame::loopseam
