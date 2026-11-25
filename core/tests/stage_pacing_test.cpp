// Host-side checks for core/stage/StagePacing: one stage's vsync callback on the vsync thread
// (docs/agents/native-stage.md).
//
// What is pinned: a callback that re-posts while running and lapses once stopped; two starts posting once; a stop and
// a start between two vsyncs posting nothing new.
#include <cstdio>

#include "stage/StagePacing.hpp"

using namespace geode::stage;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

void startAndStop() {
    StagePacing p;
    check(!p.running() && !p.posted(), "a new callback waits for its start");
    check(p.start(), "the first start posts a callback");
    check(!p.start(), "a second start posts nothing: two callbacks would fire twice per vsync");
    check(p.vsyncArrived(), "a running callback re-posts");
    p.stop();
    check(!p.vsyncArrived() && !p.posted(), "a stopped callback lapses and nothing is pending");
    check(p.start(), "a start after the lapse posts again");

    // Stop and start between two vsyncs: the pending callback carries on, nothing new is posted.
    p.stop();
    check(!p.start(), "a start while the old callback is pending posts nothing");
    check(p.vsyncArrived(), "the pending callback re-posts for the restarted one");
}

}  // namespace

int main() {
    startAndStop();
    if (failures) {
        std::fprintf(stderr, "%d stage pacing check(s) failed\n", failures);
        return 1;
    }
    std::printf("stage pacing: all checks passed\n");
    return 0;
}
