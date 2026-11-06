// Host-side checks for core/viz/frame/LoopSeam (#603): the loop render's seam driver, which moved out of
// export/LoopRender.kt (drawFrames), plus the dissolve weight it
// blends at (ExportPassMath.hpp, exportmath::seam::weight). Built and run by CI ("Native host tests" in
// .github/workflows/native-host-tests.yml) with the system compiler; no GL.
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

}  // namespace

int main() {
    weightCurve();
    schedule();
    if (failures == 0) std::printf("loop_seam_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
