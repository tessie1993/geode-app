#pragma once

// The loop render's seam driver (#603): which source frames a loop stashes and which it presents, at what
// presentation time, and how long a seam the GPU can afford. Pure maths, no GL, so core/tests/loop_seam_test.cpp
// pins it on the host; geode_export_seam_frame and geode_export_seam_budgeted_crossfade_ms
// (core/api/geode_export_pass_api.cpp) run it beside the SeamPass of ExportPasses.hpp. Each function is a
// port of the Kotlin it replaced (export/LoopRender.kt's drawFrames and budgetedSpec), integer for integer,
// because a loop rendered before and after the move must be the same file.
//
// The schedule. A stop renders loopFrames + seamFrames source frames. The opening seamFrames are drawn but
// not presented: they are copied into the stash and laid back over the closing seamFrames at the dissolve's
// weight (ExportPassMath.hpp, exportmath::seam::weight), so the loop's last frame dissolves into what the
// viewer is about to see again. Output frame j is source frame j + seamFrames, which keeps the presented
// timestamps monotonic from zero and the encoder in a single pass.

#include <cstdint>

namespace geode::viz::frame::loopseam {

// How much graphics memory the seam stash may claim. Generous enough for a second of 1080p, small enough to
// leave the scene's own targets room on a mid-range phone.
constexpr int64_t kStashBudgetBytes = 192LL * 1024 * 1024;
// One RGBA8 texture per stashed frame.
constexpr int64_t kBytesPerPixel = 4;

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

// Kotlin's `1_000_000_000L / fps`: whole nanoseconds per frame, truncated (a 30 fps frame is 33 333 333 ns).
// No frame rate reads as one frame a second rather than a division by zero.
inline int64_t frameDurationNs(int fps) { return 1'000'000'000LL / (fps > 0 ? fps : 1); }

// The presentation time of a presented source frame: the output frame's index times the truncated frame
// duration, as the encoder surface was always given it (eglPresentationTimeANDROID, nanoseconds).
inline int64_t ptsNs(int frame, int seamFrames, int fps) {
    return static_cast<int64_t>(frame - seamFrames) * frameDurationNs(fps);
}

// How many full-resolution stash frames fit kStashBudgetBytes at width x height; never under one, so even
// a frame past the budget keeps a one-frame seam rather than none. An empty frame costs nothing, so its
// budget is unbounded (the Kotlin divided by zero there; no render reaches it).
inline int affordableFrames(int width, int height) {
    const int64_t perFrame = static_cast<int64_t>(width) * height * kBytesPerPixel;
    if (perFrame <= 0) return 2147483647;
    const int64_t affordable = kStashBudgetBytes / perFrame;
    return affordable < 1 ? 1 : static_cast<int>(affordable > 2147483647 ? 2147483647 : affordable);
}

// The crossfade a LoopSpec keeps once the stash budget has had its say. crossfadeFrames is the spec's own
// frame count for crossfadeMs at fps (LoopSpec.crossfadeFrames: at least one, at most half the loop). When
// it fits, crossfadeMs comes back as it was; otherwise the longest affordable seam, in whole milliseconds
// and never under one. The stash keeps one frame per crossfade frame, so the affordable seam falls as the
// render size rises: a second of 4K costs four times what a second of 1080p does.
inline int64_t budgetedCrossfadeMs(int64_t crossfadeMs, int crossfadeFrames, int fps, int width, int height) {
    const int affordable = affordableFrames(width, height);
    if (crossfadeFrames <= affordable || fps <= 0) return crossfadeMs;
    const int64_t shortened = static_cast<int64_t>(affordable) * 1000LL / fps;
    return shortened < 1 ? 1 : shortened;
}

}  // namespace geode::viz::frame::loopseam
