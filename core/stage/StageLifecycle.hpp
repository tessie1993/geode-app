#pragma once
#include <cstdint>

#include "viz/SurfaceRun.hpp"

// When a stage creates, uses and destroys its EGL context and window surface, runs its vsync and tells its client
// (docs/agents/native-stage.md). Pure, with no EGL and no Android, so the host can test it
// (core/tests/stage_lifecycle_test.cpp); core/stage/android only carries out what it asks for.
//
// It compares what is wanted (from the events so far) with what exists (from the outcomes of its own actions)
// and asks for one action at a time. The executor reports each outcome before asking again, so a failed create
// never leaves a stale plan, and events in any order or repeated converge to the same state.
namespace geode::stage {

using RunMode = viz::surface::RunMode;

// A view stage keeps its surface while hidden (the app's view at alpha 0 keeps its Surface); a surface stage
// (the live wallpaper, the dream) pauses when its rule says Stopped, as both services called onPause there.
enum class Kind : int { View = 0, Surface = 1 };

// What the shell observes of the app's view. Values are the ABI (GEODE_STAGE_VIEW_* in geode_stage_api.h).
enum ViewFlag : uint32_t {
    kViewAttached = 1u << 0,
    kViewWindowVisible = 1u << 1,
    kViewShown = 1u << 2,   // the view and every ancestor visible
    kViewOpaque = 1u << 3,  // alpha above 0: the app hides the stage off Visuals with alpha 0 (#852)
};

// The view moves while somebody can see it; the host's pause is a separate input (Lifecycle::pause).
inline RunMode viewMode(uint32_t flags) {
    constexpr uint32_t all = kViewAttached | kViewWindowVisible | kViewShown | kViewOpaque;
    return (flags & all) == all ? RunMode::Animated : RunMode::Stopped;
}

// The tilt sensor runs only for a view somebody can see, in a host that is not paused, with the setting on.
inline bool gyroListens(uint32_t flags, bool paused, bool wanted) {
    return wanted && !paused && viewMode(flags) == RunMode::Animated;
}

enum class Act : uint8_t {
    None,
    CreateContext,
    CreateSurface,
    MakeCurrent,
    MakeCurrentSurfaceless,
    ClientSurfaceCreated,
    ClientSurfaceChanged,  // width, height
    StartVsync,
    StopVsync,
    DrawOnce,
    ClientTrimSpares,
    ClientContextLost,
    ReleaseCurrent,
    DestroySurface,  // releases the current context first
    DestroyContext,
    ClientRelease,  // glCurrent
    ReportState,    // state
};

struct Action {
    constexpr Action(Act a = Act::None, int w = 0, int h = 0, bool gl = false, uint32_t s = 0)
        : act(a), width(w), height(h), glCurrent(gl), state(s) {}
    Act act;
    int width;
    int height;
    bool glCurrent;
    uint32_t state;
};

// How an action ended. Only the creates and the make-currents can fail.
enum class Outcome : uint8_t { Done, Refused, BadSurface, ContextLost };

// The state word the shell polls. Values are the ABI (GEODE_STAGE_STATE_* in geode_stage_api.h).
inline constexpr uint32_t kStateWindow = 1u << 0;
inline constexpr uint32_t kStateContext = 1u << 1;
inline constexpr uint32_t kStateSurface = 1u << 2;
inline constexpr uint32_t kStateDrawable = 1u << 3;
inline constexpr uint32_t kStateVsync = 1u << 4;
inline constexpr uint32_t kStatePaused = 1u << 5;
inline constexpr uint32_t kStateRefused = 1u << 6;
inline constexpr uint32_t kStateSurfaceBad = 1u << 7;
inline constexpr uint32_t kStateReleased = 1u << 8;
inline constexpr uint32_t kStateModeShift = 12;  // RunMode in bits 12-13

class Lifecycle {
public:
    // Losses in a row, with no frame drawn between them, before the context counts as refused.
    static constexpr int kMaxLostInARow = 3;

    explicit Lifecycle(Kind kind) : kind_(kind) {}

    // Events. Each may come at any time, in any order, any number of times; after quit they change nothing.
    void windowSet(int width, int height);
    void windowChanged(int width, int height);
    void windowGone();
    void setMode(RunMode mode, int64_t nowMs);
    void tick(int64_t nowMs);
    void pause();
    void resume();
    void redrawNeeded();
    void contextLost();
    void surfaceBad();
    void frameDrawn();
    void surfacelessSupported(bool supported);
    void quit();

    // The next action to carry out, None when nothing is owed. Ask again after reporting each outcome.
    Action next() const;
    void done(const Action& action, Outcome outcome);

    // A frame may be drawn now: the context is current, the client built and sized, the surface wanted.
    bool canDraw() const;
    bool vsyncOn() const { return vsync_; }
    // When a tick is owed (the end of a still frame's settle), -1 for none.
    int64_t deadlineMs() const { return settling_ ? settleUntilMs_ : -1; }
    uint32_t stateWord() const;
    // Quit and every resource gone: the stage thread may end.
    bool finished() const { return quit_ && released_ && !context_ && !surface_ && !surfaceless_ && !vsync_; }
    RunMode mode() const { return mode_; }

private:
    bool wantsSurface() const;
    bool wantsDrawing() const;
    bool drawable() const;
    bool wantsVsync() const;
    bool glCurrent() const { return current_ || surfaceless_; }
    bool surfaceUsable() const { return surface_ && !surfaceBad_ && !stale_ && window_; }
    void noteDrawing(bool before);
    void markLost();

    Kind kind_;
    // Wanted, from events.
    bool window_ = false;
    int width_ = 0;
    int height_ = 0;
    bool paused_ = false;
    RunMode mode_ = RunMode::Stopped;
    bool settling_ = false;
    int64_t settleUntilMs_ = 0;
    bool drawOnce_ = false;
    bool trimDue_ = false;
    bool surfacelessOk_ = false;
    bool quit_ = false;
    // What exists, from outcomes.
    bool context_ = false;
    bool surface_ = false;
    bool stale_ = false;  // the surface belongs to a window since replaced
    bool current_ = false;
    bool surfaceless_ = false;
    bool clientCreated_ = false;
    int toldWidth_ = 0;
    int toldHeight_ = 0;
    bool vsync_ = false;
    bool lost_ = false;
    bool refused_ = false;
    bool surfaceBad_ = false;
    int lostInARow_ = 0;
    bool released_ = false;
    uint32_t reported_ = ~0u;
};

}  // namespace geode::stage
