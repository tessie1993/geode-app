// Host-side checks for core/viz/frame/LoopSeam (#603): the loop render's seam driver, which moved out of
// export/LoopRender.kt (drawFrames; its progress and reported crossfade, #752), plus the dissolve weight it
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

void progress() {
    // A three-stop reel whose later stops kept fewer seam frames, as renderStops' shrinking seam allows.
    const int stops = 3;
    const int loopFrames = 240;
    const int seams[stops] = {24, 18, 18};
    float last = 0.0f;
    bool rising = true;
    bool inRange = true;
    for (int stop = 0; stop < stops; ++stop) {
        for (int f = 0; f < loopFrames + seams[stop]; ++f) {
            const float p = ls::progress(stop, stops, f, loopFrames, seams[stop]);
            rising = rising && p > last;
            inRange = inRange && p > 0.0f && p <= 1.0f;
            last = p;
        }
        // Each stop ends on its share exactly.
        const float end = ls::progress(stop, stops, loopFrames + seams[stop] - 1, loopFrames, seams[stop]);
        check(end == static_cast<float>(static_cast<double>(stop + 1) / stops), "progress: a stop ends on its share");
    }
    check(rising, "progress: strictly rising across every stop");
    check(inRange, "progress: stays in 0..1");
    check(last == 1.0f, "progress: the last frame of the last stop is exactly 1");

    // Any stop count and odd frame counts still land on exactly 1.
    for (int n = 1; n <= 12; ++n) {
        check(ls::progress(n - 1, n, 37 + 5 - 1, 37, 5) == 1.0f, "progress: exactly 1 at the end for any stop count");
    }
    // A single stop matches the Kotlin it replaced: base 0 + span 1 * (frame + 1) / sourceFrames.
    check(ls::progress(0, 1, 0, 10, 3) == 1.0f / 13.0f, "progress: one stop's first frame is 1 / sourceFrames");
    // Stray input stays in range.
    check(ls::progress(5, 3, 99, 10, 3) == 1.0f, "progress: past the end clamps to 1");
    check(ls::progress(-2, 3, 0, 10, 3) == 0.0f, "progress: before the start clamps to 0");
    check(ls::progress(0, 0, 0, 10, 3) == 1.0f, "progress: no stops reads as done");
    check(ls::progress(0, 1, 0, 0, 0) == 1.0f, "progress: an empty stop reads as done");
}

void reportedCrossfade() {
    // The Kotlin it replaced: seamFrames * 1000L / fps.
    check(ls::reportedCrossfadeMs(30, 30) == 1000, "report: 30 frames at 30 fps is one second");
    check(ls::reportedCrossfadeMs(7, 30) == 233, "report: truncated to whole ms");
    check(ls::reportedCrossfadeMs(3, 0) == 3000, "report: no frame rate reads as one frame a second");
}

}  // namespace

int main() {
    weightCurve();
    schedule();
    progress();
    reportedCrossfade();
    if (failures == 0) std::printf("loop_seam_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
