#pragma once
#include <algorithm>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

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
//  - GiveUp: the stager has had work for kStallSeconds and finished none of it (a driver that never
//    completes a parallel compile). The renderer reports it and stops asking, as for a failed build.
//
// A target that failed (its programs did not build, or it gave up) is remembered, so the renderer keeps
// drawing the scene it had instead of building the broken one again every frame, until forgive().
class ScenePrep {
public:
    static constexpr int kMaxAttempts = 6;
    static constexpr double kStallSeconds = 30.0;

    enum class Step { Wait, Attempt, Block, GiveUp };

    // True when `id` is not the scene being prepared, which restarts the count; the caller then
    // drops whatever the stager holds for the old target.
    bool retarget(const std::string& id) {
        if (active_ && id == target_) return false;
        target_ = id;
        active_ = true;
        attempts_ = 0;
        stallSinceS_ = -1.0;
        return true;
    }

    // Once per frame after retarget(). `stagerIdle`: nothing queued or compiling. `progress`: a count
    // that moves whenever the stager finishes a piece of work (ProgramStager::progress).
    Step next(bool stagerIdle, double nowS = 0.0, int progress = 0) {
        if (attempts_ >= kMaxAttempts) return Step::Block;
        if (!stagerIdle) {
            if (stallSinceS_ < 0.0 || progress != progress_) {
                stallSinceS_ = nowS;
                progress_ = progress;
            }
            return nowS - stallSinceS_ > kStallSeconds ? Step::GiveUp : Step::Wait;
        }
        stallSinceS_ = -1.0;
        ++attempts_;
        return Step::Attempt;
    }

    // The target is built (or abandoned): the next request starts fresh.
    void done() {
        active_ = false;
        target_.clear();
        attempts_ = 0;
        stallSinceS_ = -1.0;
    }

    // Lasting: a style quarantined for crashing this driver (StyleQuarantine), which no new context or
    // shader edit of another style makes safe to try in this process. A Studio override of the style
    // itself is a different shader, so it may build (overridden()).
    enum class Lasting { No, Yes };

    // `id` did not build: not asked for again until forgive(). `report` is what the stage says about it.
    void fail(const std::string& id, const std::string& report = {}, Lasting lasting = Lasting::No) {
        auto& list = lasting == Lasting::Yes ? lasting_ : failed_;
        if (!find(list, id)) list.push_back(Failure{id, report});
    }
    bool failed(const std::string& id) const { return find(failed_, id) != nullptr || quarantined(id); }
    // Off for crashing this driver, and not drawn by an override.
    bool quarantined(const std::string& id) const { return find(lasting_, id) != nullptr && !overridden(id); }
    // What fail() was told about `id`, said again when the style is picked again; empty for none.
    std::string report(const std::string& id) const {
        if (quarantined(id)) return find(lasting_, id)->report;
        const auto* entry = find(failed_, id);
        return entry ? entry->report : std::string();
    }
    // True when `text` is the report of a failed style, so it can be taken down for another style.
    bool reported(const std::string& text) const {
        if (text.empty()) return false;
        const auto same = [&](const Failure& entry) { return entry.report == text; };
        return std::any_of(failed_.begin(), failed_.end(), same) || std::any_of(lasting_.begin(), lasting_.end(), same);
    }
    // A new context or a changed shader source may build what failed before.
    void forgive() { failed_.clear(); }

    // The styles a Studio shader override draws (viz/glsl/ShaderOverrides).
    void setOverridden(std::vector<std::string> ids) { overridden_ = std::move(ids); }
    bool overridden(const std::string& id) const { return std::find(overridden_.begin(), overridden_.end(), id) != overridden_.end(); }

    // The style a live frame stages for `requested`: itself, unless it is quarantined; then the style on
    // screen, else `fallback`, whichever has not failed; `requested` again when neither will do.
    const std::string& stageFor(const std::string& requested, const std::string& onScreen, const std::string& fallback) const {
        if (!quarantined(requested)) return requested;
        for (const std::string* candidate : {&onScreen, &fallback}) {
            if (!candidate->empty() && !failed(*candidate)) return *candidate;
        }
        return requested;
    }

    // True when `id`, a secondary target (the layer, Liquid's next style), must wait because the
    // stager is working for `other`, the other secondary target, which is still wanted. Without this
    // the two retarget each other every frame, so the stager is cleared and neither build ever
    // finishes (#369). A target nobody asks for any more (`other` empty, or not the one being
    // prepared) is taken over instead, so a stale target never blocks.
    bool yieldsTo(const std::string& id, const std::string& other) const {
        return active_ && !other.empty() && other != id && target_ == other;
    }

    bool active() const { return active_; }
    const std::string& target() const { return target_; }
    int attempts() const { return attempts_; }

private:
    std::string target_;
    bool active_ = false;
    int attempts_ = 0;
    double stallSinceS_ = -1.0;
    int progress_ = 0;
    struct Failure {
        std::string id;
        std::string report;
    };
    static const Failure* find(const std::vector<Failure>& list, const std::string& id) {
        const auto it = std::find_if(list.begin(), list.end(), [&](const auto& entry) { return entry.id == id; });
        return it == list.end() ? nullptr : &*it;
    }

    std::vector<Failure> failed_;
    std::vector<Failure> lasting_;
    std::vector<std::string> overridden_;
};

}  // namespace geode::viz
