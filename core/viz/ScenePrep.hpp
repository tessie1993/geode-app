#pragma once
#include <string>

// The pure half of staged scene builds (ProgramStager is the GL half). No GL here, so the host
// tests (core/tests/viz_math_test.cpp) drive it frame by frame.
//
// Why staged at all: a style's first build compiles and links every program it draws with, and
// on a software GPU (the CI emulator, slow phones) that is seconds of work. Done inside one
// onDrawFrame, it holds the GLSurfaceView GL thread, and every main-thread call that waits on
// that thread (onPause, onResume, surfaceDestroyed, surfaceChanged) blocks main for the whole
// build: an ANR. So a live renderer builds a new scene across frames, a bounded slice per frame,
// and keeps drawing what it was drawing until the new scene is whole.
namespace geode::viz {

// What one frame may spend on queued program work. The first piece always goes, so a frame
// that is already late still moves the build along; after it, more pieces go while the frame is
// inside its time slice. A blocking compile (no GL_KHR_parallel_shader_compile, so the driver
// compiles on this thread) is capped at one per frame because its cost has no bound we can see
// in advance; cache loads and parallel kicks are cheap and only the time slice limits them.
class BuildBudget {
public:
    static constexpr double kFrameSeconds = 0.008;
    static constexpr int kBlockingPerFrame = 1;

    void begin(double nowS) {
        startS_ = nowS;
        pieces_ = 0;
        blocking_ = 0;
    }

    bool allows(double nowS, bool blocking) const {
        if (blocking && blocking_ >= kBlockingPerFrame) return false;
        if (pieces_ == 0) return true;
        return nowS - startS_ < kFrameSeconds;
    }

    void spend(bool blocking) {
        ++pieces_;
        if (blocking) ++blocking_;
    }

    int pieces() const { return pieces_; }

private:
    double startS_ = 0.0;
    int pieces_ = 0;
    int blocking_ = 0;
};

// The scene a live frame wants but has not built yet, and what this frame does about it.
//
//  - Attempt: build the scene now with every program build routed through the stager. While any
//    program is not compiled yet the stager hands out a placeholder and marks the attempt
//    deferred; the renderer then drops the half-built scene and keeps drawing the old one. The
//    first attempt therefore only discovers what the scene needs; the next one, made once the
//    stager is idle, takes the compiled programs and succeeds.
//  - Wait: the stager still has work queued or compiling; advance it one budgeted step.
//  - Block: too many attempts came back deferred (a scene whose program list keeps changing),
//    so build it the old way, all at once, rather than never showing it.
class ScenePrep {
public:
    static constexpr int kMaxAttempts = 6;

    enum class Step { Wait, Attempt, Block };

    // True when `id` is not the scene being prepared, which restarts the count; the caller then
    // drops whatever the stager holds for the old target.
    bool retarget(const std::string& id) {
        if (active_ && id == target_) return false;
        target_ = id;
        active_ = true;
        attempts_ = 0;
        return true;
    }

    // Once per frame after retarget(). `stagerIdle`: nothing queued or compiling.
    Step next(bool stagerIdle) {
        if (attempts_ >= kMaxAttempts) return Step::Block;
        if (!stagerIdle) return Step::Wait;
        ++attempts_;
        return Step::Attempt;
    }

    // The target is built (or abandoned): the next request starts fresh.
    void done() {
        active_ = false;
        target_.clear();
        attempts_ = 0;
    }

    bool active() const { return active_; }
    const std::string& target() const { return target_; }
    int attempts() const { return attempts_; }

private:
    std::string target_;
    bool active_ = false;
    int attempts_ = 0;
};

}  // namespace geode::viz
