// Host-side checks for core/viz/frame/LoopSeam (#603): the loop render's seam driver, which moved out of
// export/LoopRender.kt (drawFrames, budgetedSpec, SEAM_STASH_BUDGET_BYTES), plus the dissolve weight it
// blends at (ExportPassMath.hpp, exportmath::seam::weight). Built and run by CI ("Native host tests" in
// .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: the numbers the Kotlin computed before the move (worked from its expressions, integer for
// integer), because a loop rendered before and after must be the same file.
#include <cstdint>
#include <cstdio>

#include "viz/frame/ExportPassMath.hpp"
#include "viz/frame/LoopSeam.hpp"

namespace ls = geode::viz::frame::loopseam;
namespace seam = geode::viz::frame::exportmath::seam;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// LoopSpec.crossfadeFrames: (crossfadeMs * fps / 1000).toInt().coerceIn(1, loopFrames / 2).
int kotlinCrossfadeFrames(int64_t crossfadeMs, int fps, int loopFrames) {
    int64_t f = crossfadeMs * fps / 1000;
    if (f > loopFrames / 2) f = loopFrames / 2;
    if (f < 1) f = 1;
    return static_cast<int>(f);
}

void weightCurve() {
    // The ends are exact: no weight on the first closing frame, all of it on the last.
    check(seam::weight(0, 5) == 0.0f, "weight: first closing frame is the plain render");
    check(seam::weight(4, 5) == 1.0f, "weight: last closing frame is all stash");
    check(seam::weight(2, 5) == 0.5f, "weight: middle of five is half");
    check(seam::weight(1, 3) == 0.5f, "weight: middle of three is half");
    check(seam::weight(0, 1) == 1.0f, "weight: a one-frame seam is all stash");
    check(seam::weight(-1, 5) == 0.0f, "weight: clamps below");
    check(seam::weight(9, 5) == 1.0f, "weight: clamps above");
    float last = -1.0f;
    bool rising = true;
    for (int t = 0; t < 24; ++t) {
        const float w = seam::weight(t, 24);
        rising = rising && w > last;
        last = w;
    }
    check(rising, "weight: strictly rising across the seam");
}

void schedule() {
    // A stop of 10 loop frames and a 3-frame seam renders 13 source frames.
    const int loopFrames = 10;
    const int seamFrames = 3;
    for (int f = 0; f < seamFrames; ++f) {
        const ls::Step s = ls::step(f, loopFrames, seamFrames);
        check(s.op == ls::Op::Capture && s.index == f, "schedule: opening frames are stashed in order");
    }
    for (int f = seamFrames; f < loopFrames; ++f) {
        check(ls::step(f, loopFrames, seamFrames).op == ls::Op::Present, "schedule: middle frames are presented plain");
    }
    for (int f = loopFrames; f < loopFrames + seamFrames; ++f) {
        const ls::Step s = ls::step(f, loopFrames, seamFrames);
        const int tail = f - loopFrames;
        check(s.op == ls::Op::BlendPresent && s.index == tail && s.tail == tail,
              "schedule: closing frames blend stash frame `tail` at `tail`");
    }
    int presented = 0;
    for (int f = 0; f < loopFrames + seamFrames; ++f) {
        if (ls::step(f, loopFrames, seamFrames).op != ls::Op::Capture) ++presented;
    }
    check(presented == loopFrames, "schedule: exactly loopFrames frames reach the encoder");
    // No seam: everything is presented, the last frame (tail 0 of a 0-frame seam) included.
    check(ls::step(0, 4, 0).op == ls::Op::Present, "schedule: no seam presents the first frame");
}

void pts() {
    check(ls::frameDurationNs(30) == 33'333'333, "pts: 30 fps truncates");
    check(ls::frameDurationNs(60) == 16'666'666, "pts: 60 fps truncates");
    check(ls::frameDurationNs(24) == 41'666'666, "pts: 24 fps truncates");
    check(ls::frameDurationNs(0) == 1'000'000'000, "pts: no frame rate is one a second, not a crash");
    check(ls::ptsNs(3, 3, 30) == 0, "pts: the first presented frame is at zero");
    check(ls::ptsNs(12, 3, 30) == 9LL * 33'333'333, "pts: output frame index times the truncated duration");
    // Kotlin: (frame - seamFrames) * frameDurationNs in Long, so a long loop does not wrap.
    check(ls::ptsNs(200'000, 0, 60) == 200'000LL * 16'666'666, "pts: 64-bit");
    int64_t last = -1;
    bool rising = true;
    for (int f = 0; f < 13; ++f) {
        if (ls::step(f, 10, 3).op == ls::Op::Capture) continue;
        const int64_t t = ls::ptsNs(f, 3, 30);
        rising = rising && t > last;
        last = t;
    }
    check(rising, "pts: presented timestamps rise monotonically");
}

void budget() {
    check(ls::kStashBudgetBytes == 201'326'592, "budget: 192 MB");
    // 1080p: 8 294 400 bytes a frame, 24 of them in 192 MB; 4K is a quarter of that.
    check(ls::affordableFrames(1920, 1080) == 24, "budget: 24 frames of 1080p");
    check(ls::affordableFrames(1080, 1920) == 24, "budget: portrait the same");
    check(ls::affordableFrames(3840, 2160) == 6, "budget: 6 frames of 4K");
    check(ls::affordableFrames(1280, 720) == 54, "budget: 54 frames of 720p");
    check(ls::affordableFrames(20000, 20000) == 1, "budget: never under one frame");

    // Fits: the spec comes back as it was.
    check(ls::budgetedCrossfadeMs(1000, 30, 30, 1280, 720) == 1000, "budget: a fitting seam is unchanged");
    check(ls::budgetedCrossfadeMs(800, 24, 30, 1920, 1080) == 800, "budget: exactly the budget fits");
    // Over: the longest affordable seam in whole ms (Kotlin: affordable * 1000L / fps).
    check(ls::budgetedCrossfadeMs(1000, 30, 30, 1920, 1080) == 800, "budget: 1080p30 one second becomes 800 ms");
    check(ls::budgetedCrossfadeMs(1000, 60, 60, 3840, 2160) == 100, "budget: 4K60 one second becomes 100 ms");
    check(ls::budgetedCrossfadeMs(1000, 60, 60, 1920, 1080) == 400, "budget: 1080p60 one second becomes 400 ms");
    check(ls::budgetedCrossfadeMs(50, 5, 2000, 20000, 20000) == 1, "budget: never under 1 ms");

    // The shortened spec's own frame count then fits the stash (LoopSpec.crossfadeFrames over it).
    const int loopFrames = 30 * 8;
    const int64_t ms = ls::budgetedCrossfadeMs(1500, kotlinCrossfadeFrames(1500, 30, loopFrames), 30, 1920, 1080);
    check(kotlinCrossfadeFrames(ms, 30, loopFrames) <= ls::affordableFrames(1920, 1080), "budget: the shortened seam fits");
}

}  // namespace

int main() {
    weightCurve();
    schedule();
    pts();
    budget();
    if (failures == 0) std::printf("loop_seam_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
