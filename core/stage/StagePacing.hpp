#pragma once

// One stage's vsync callback on the vsync thread: whether it runs, whether one is posted.
// Whether a vsync draws is stage/VsyncRule's, and the window's frame-rate hint is the stage thread's own. Pure, with no
// Android, so the host can test it (core/tests/stage_pacing_test.cpp).
//
// Confined to the vsync thread: start and stop arrive there as commands, and the choreographer callback calls
// vsyncArrived.
namespace geode::stage {

class StagePacing {
public:
    bool running() const { return running_; }
    // A callback is posted and not yet delivered.
    bool posted() const { return posted_; }

    // True when the caller must post a callback now. With a callback still pending (a stop and a start between two
    // vsyncs) nothing is posted: there is no call that removes a posted callback, and a second one would fire twice
    // per vsync.
    bool start() {
        if (running_) return false;
        running_ = true;
        if (posted_) return false;
        posted_ = true;
        return true;
    }

    // The pending callback lapses on its next vsync: it sees the callback stopped and neither re-posts nor draws.
    void stop() { running_ = false; }

    // The posted callback arrived. True: the caller re-posts at once, so a skipped vsync still wakes the next one.
    // False: stopped, nothing is pending any more.
    bool vsyncArrived() {
        if (!running_) {
            posted_ = false;
            return false;
        }
        return true;
    }

private:
    bool running_ = false;
    bool posted_ = false;
};

}  // namespace geode::stage
