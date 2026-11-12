// Host-side checks for core/stage/StageLifecycle: when a stage makes, uses and frees its EGL context and window
// surface, runs its vsync and tells its client (docs/agents/native-stage.md). No EGL: a model executor carries out
// each action, checks it was allowed, and can fail the creates the way a driver does.
//
// What is pinned: the action order of the common paths (a view shown, paused and resumed with its context kept,
// hidden while resumed, rotated, its context lost; a wallpaper's still frame with its settle burst, a restated
// still frame, Dark; a refused context; three losses in a row), and, over every sequence of four events from a
// twenty-event alphabet and thousands of longer random ones, for both kinds: no GL call without a current context,
// no surface on a gone or replaced window once the events are applied, every create matched by its destroy after
// quit, the client released exactly once, and nothing at all after quit.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "stage/StageLifecycle.hpp"

using namespace geode::stage;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

const char* name(Act a) {
    switch (a) {
        case Act::None: return "None";
        case Act::CreateContext: return "CreateContext";
        case Act::CreateSurface: return "CreateSurface";
        case Act::MakeCurrent: return "MakeCurrent";
        case Act::MakeCurrentSurfaceless: return "MakeCurrentSurfaceless";
        case Act::ClientSurfaceCreated: return "ClientSurfaceCreated";
        case Act::ClientSurfaceChanged: return "ClientSurfaceChanged";
        case Act::StartVsync: return "StartVsync";
        case Act::StopVsync: return "StopVsync";
        case Act::DrawOnce: return "DrawOnce";
        case Act::ClientTrimSpares: return "ClientTrimSpares";
        case Act::ClientContextLost: return "ClientContextLost";
        case Act::ReleaseCurrent: return "ReleaseCurrent";
        case Act::DestroySurface: return "DestroySurface";
        case Act::DestroyContext: return "DestroyContext";
        case Act::ClientRelease: return "ClientRelease";
        case Act::ReportState: return "ReportState";
    }
    return "?";
}

// What exists on the "device", and what the next create or make-current will do.
struct Model {
    int window = 0;          // the window the shell has handed over, 0 none
    int surfaceWindow = 0;   // the window the EGL surface was made on, 0 no surface
    bool context = false;
    bool current = false;    // on the window surface
    bool surfaceless = false;
    bool clientBuilt = false;
    bool vsync = false;
    int releases = 0;
    bool releasedWithGl = false;
    int contextCreates = 0, contextDestroys = 0, surfaceCreates = 0, surfaceDestroys = 0;
    bool failNextContext = false;
    bool failNextSurface = false;
    bool loseOnNextCurrent = false;
    bool surfacelessOk = true;
    bool quit = false;
    int afterQuit = 0;       // actions other than a state report once quit had settled
    bool settledAfterQuit = false;
    bool violated = false;
    const char* violation = "";

    void violate(const char* what) {
        if (!violated) violation = what;
        violated = true;
    }
    bool glCurrent() const { return current || surfaceless; }
};

Outcome execute(Model& m, const Action& a) {
    if (m.settledAfterQuit && a.act != Act::ReportState) m.afterQuit++;
    switch (a.act) {
        case Act::None: break;
        case Act::CreateContext:
            if (m.context) m.violate("a second context was made");
            if (m.failNextContext) {
                m.failNextContext = false;
                return Outcome::Refused;
            }
            m.context = true;
            m.clientBuilt = false;
            m.contextCreates++;
            break;
        case Act::CreateSurface:
            if (!m.context) m.violate("a surface was made with no context");
            if (m.surfaceWindow != 0) m.violate("a second surface was made");
            if (m.window == 0) m.violate("a surface was made with no window");
            if (m.failNextSurface) {
                m.failNextSurface = false;
                return Outcome::BadSurface;
            }
            m.surfaceWindow = m.window;
            m.surfaceCreates++;
            break;
        case Act::MakeCurrent:
            if (!m.context || m.surfaceWindow == 0) m.violate("made current with no context or surface");
            if (m.loseOnNextCurrent) {
                m.loseOnNextCurrent = false;
                return Outcome::ContextLost;
            }
            m.current = true;
            m.surfaceless = false;
            break;
        case Act::MakeCurrentSurfaceless:
            if (!m.context) m.violate("surfaceless with no context");
            if (!m.surfacelessOk) return Outcome::Refused;
            m.surfaceless = true;
            break;
        case Act::ClientSurfaceCreated:
            if (!m.glCurrent()) m.violate("surfaceCreated without a current context");
            m.clientBuilt = true;
            break;
        case Act::ClientSurfaceChanged:
            if (!m.current) m.violate("surfaceChanged without a current window surface");
            if (a.width <= 0 || a.height <= 0) m.violate("surfaceChanged with an empty size");
            break;
        case Act::StartVsync:
            if (m.vsync) m.violate("vsync started twice");
            if (!m.current || !m.clientBuilt) m.violate("vsync started with nothing to draw on");
            m.vsync = true;
            break;
        case Act::StopVsync:
            if (!m.vsync) m.violate("vsync stopped while stopped");
            m.vsync = false;
            break;
        case Act::DrawOnce:
            if (!m.current || !m.clientBuilt) m.violate("a frame without a current context");
            break;
        case Act::ClientTrimSpares:
            if (!m.glCurrent() || !m.clientBuilt) m.violate("trimSpares without a current context");
            break;
        case Act::ClientContextLost:
            if (!m.clientBuilt) m.violate("contextLost for a client never built");
            if (m.glCurrent()) m.violate("contextLost with the lost context still current");
            m.clientBuilt = false;
            break;
        case Act::ReleaseCurrent:
            m.surfaceless = false;
            m.current = false;
            break;
        case Act::DestroySurface:
            if (m.surfaceWindow == 0) m.violate("a surface destroyed twice");
            m.surfaceWindow = 0;
            m.current = false;
            m.surfaceDestroys++;
            break;
        case Act::DestroyContext:
            if (!m.context) m.violate("a context destroyed twice");
            if (m.surfaceWindow != 0) m.violate("a context destroyed before its surface");
            m.context = false;
            m.current = false;
            m.surfaceless = false;
            m.clientBuilt = false;
            m.contextDestroys++;
            break;
        case Act::ClientRelease:
            m.releases++;
            m.releasedWithGl = a.glCurrent;
            if (a.glCurrent && !m.glCurrent()) m.violate("release told a context is current when none is");
            break;
        case Act::ReportState: break;
    }
    return Outcome::Done;
}

// Runs the lifecycle until it owes nothing, recording the actions (state reports left out).
std::vector<Act> settle(Lifecycle& lc, Model& m) {
    std::vector<Act> acts;
    for (int i = 0; i < 64; ++i) {
        const Action a = lc.next();
        if (a.act == Act::None) {
            if (m.quit) m.settledAfterQuit = true;
            return acts;
        }
        if (a.act != Act::ReportState) acts.push_back(a.act);
        lc.done(a, execute(m, a));
    }
    m.violate("the lifecycle did not settle in 64 actions");
    return acts;
}

bool same(const std::vector<Act>& got, std::initializer_list<Act> want, const char* what) {
    const bool ok = got == std::vector<Act>(want);
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s: got", what);
        for (const Act a : got) std::fprintf(stderr, " %s", name(a));
        std::fprintf(stderr, "\n");
        failures++;
    }
    return ok;
}

// The checks that hold after any batch of events has been applied.
void invariants(const Lifecycle& lc, Model& m) {
    if (m.surfaceWindow != 0 && m.surfaceWindow != m.window) m.violate("a surface outlived its window's hand-over");
    if (lc.canDraw() && (!m.current || !m.clientBuilt)) m.violate("drawable without a built, current client");
    if (m.vsync && !lc.canDraw()) m.violate("vsync running with nothing to draw");
    if (m.vsync != lc.vsyncOn()) m.violate("the vsync the lifecycle believes is not the one running");
}

// ---- the common paths, action by action ----

void viewPaths() {
    Lifecycle lc(Kind::View);
    Model m;
    lc.surfacelessSupported(true);
    m.window = 1;
    lc.windowSet(1080, 2340);
    same(settle(lc, m), {Act::CreateContext, Act::CreateSurface, Act::MakeCurrent, Act::ClientSurfaceCreated, Act::ClientSurfaceChanged},
         "a view builds on its window at once, hidden or not");
    lc.setMode(viewMode(kViewAttached | kViewWindowVisible | kViewShown | kViewOpaque), 0);
    same(settle(lc, m), {Act::StartVsync}, "a watched view paces");
    check(lc.canDraw() && lc.vsyncOn(), "and draws");
    constexpr uint32_t watched = kViewAttached | kViewWindowVisible | kViewShown | kViewOpaque;
    check(gyroListens(watched, false, true), "the tilt sensor runs for a watched, resumed view that wants it");
    check(!gyroListens(watched, false, false) && !gyroListens(watched, true, true) && !gyroListens(watched & ~kViewOpaque, false, true),
          "and not when it is off, the host is paused or nobody can see the view");

    lc.pause();
    same(settle(lc, m), {Act::StopVsync, Act::ClientTrimSpares, Act::DestroySurface}, "a pause stops, trims, then lets the surface go");
    check(m.context && !m.current, "the context outlives the pause (preserveEGLContextOnPause)");
    lc.resume();
    same(settle(lc, m), {Act::CreateSurface, Act::MakeCurrent, Act::ClientSurfaceChanged, Act::StartVsync},
         "a resume rebuilds the surface only: no second surfaceCreated");

    lc.setMode(viewMode(kViewAttached | kViewWindowVisible | kViewShown), 0);
    same(settle(lc, m), {Act::StopVsync, Act::ClientTrimSpares}, "alpha 0 while resumed stops and trims, the surface kept");
    check(m.surfaceWindow == 1, "a hidden view keeps its surface");
    lc.setMode(RunMode::Animated, 0);
    same(settle(lc, m), {Act::StartVsync}, "shown again it paces at once");

    lc.windowChanged(2340, 1080);
    same(settle(lc, m), {Act::ClientSurfaceChanged}, "a rotation is one surfaceChanged, the vsync kept");

    lc.contextLost();
    same(settle(lc, m),
         {Act::ReleaseCurrent, Act::ClientContextLost, Act::DestroySurface, Act::DestroyContext, Act::CreateContext, Act::CreateSurface,
          Act::MakeCurrent, Act::ClientSurfaceCreated, Act::ClientSurfaceChanged},
         "a lost context is let go, forgotten, freed and rebuilt, the vsync kept");

    lc.redrawNeeded();
    same(settle(lc, m), {}, "a redraw while pacing is the next vsync's");

    m.window = 0;
    lc.windowGone();
    same(settle(lc, m), {Act::StopVsync, Act::ClientTrimSpares, Act::DestroySurface}, "the window going trims while it still can");
    lc.quit();
    m.quit = true;
    same(settle(lc, m), {Act::MakeCurrentSurfaceless, Act::ClientRelease, Act::ReleaseCurrent, Act::DestroyContext},
         "quit with no window releases on a surfaceless context, then frees it");
    check(m.releases == 1 && m.releasedWithGl, "released once, with the context current");
    check(lc.finished(), "and the stage may end");
}

void surfacePaths() {
    Lifecycle lc(Kind::Surface);
    Model m;
    m.window = 1;
    lc.windowSet(1440, 3120);
    same(settle(lc, m), {}, "a wallpaper builds nothing while its rule says stopped");
    lc.setMode(RunMode::Animated, 0);
    same(settle(lc, m), {Act::CreateContext, Act::CreateSurface, Act::MakeCurrent, Act::ClientSurfaceCreated, Act::ClientSurfaceChanged, Act::StartVsync},
         "animated builds and paces");
    lc.setMode(RunMode::Stopped, 100);
    same(settle(lc, m), {Act::StopVsync, Act::ClientTrimSpares, Act::DestroySurface}, "stopped stops, trims and pauses");
    check(m.context, "keeping the context");

    lc.setMode(RunMode::Still, 1000);
    same(settle(lc, m), {Act::CreateSurface, Act::MakeCurrent, Act::ClientSurfaceChanged, Act::StartVsync}, "into a still frame it paces to settle");
    check(lc.deadlineMs() == 1000 + 2000, "for the native settle");
    lc.tick(2999);
    same(settle(lc, m), {}, "still settling a millisecond before the end");
    lc.tick(3000);
    same(settle(lc, m), {Act::StopVsync}, "then stops pacing");
    check(lc.deadlineMs() < 0, "and owes no tick");
    lc.setMode(RunMode::Still, 4000);
    same(settle(lc, m), {Act::DrawOnce}, "a restated still frame draws once");
    lc.setMode(RunMode::Still, 4001);
    lc.setMode(RunMode::Still, 4002);
    same(settle(lc, m), {Act::DrawOnce}, "two restates in one batch draw once");

    lc.setMode(RunMode::Animated, 5000);
    same(settle(lc, m), {Act::StartVsync}, "animated again");
    lc.setMode(RunMode::Dark, 6000);
    same(settle(lc, m), {Act::StopVsync}, "dark stops pacing without trimming, the GL kept");
    check(m.surfaceWindow == 1 && m.context, "dark keeps surface and context");
    lc.setMode(RunMode::Still, 7000);
    same(settle(lc, m), {Act::StartVsync}, "dark to still settles too");

    // A rotation replaces the wallpaper's surface: the old one goes before the new one is made.
    m.window = 2;
    lc.windowSet(3120, 1440);
    same(settle(lc, m), {Act::DestroySurface, Act::CreateSurface, Act::MakeCurrent, Act::ClientSurfaceChanged},
         "a replaced window: the old surface first, then the new one, the vsync kept");

    lc.quit();
    m.quit = true;
    same(settle(lc, m), {Act::StopVsync, Act::ClientRelease, Act::DestroySurface, Act::DestroyContext},
         "quit on a live surface releases with it current, then frees");
    check(m.releasedWithGl, "with the GL current");
}

void refusedAndLost() {
    Lifecycle lc(Kind::View);
    Model m;
    m.window = 1;
    m.failNextContext = true;
    lc.windowSet(800, 600);
    lc.setMode(RunMode::Animated, 0);
    same(settle(lc, m), {Act::CreateContext}, "a refused context is asked once");
    check((lc.stateWord() & kStateRefused) != 0, "and reported");
    lc.pause();
    lc.resume();
    same(settle(lc, m), {}, "not asked again for a resume");
    lc.windowSet(800, 600);
    same(settle(lc, m), {Act::CreateContext, Act::CreateSurface, Act::MakeCurrent, Act::ClientSurfaceCreated, Act::ClientSurfaceChanged, Act::StartVsync},
         "a new window tries again");

    m.failNextSurface = true;
    m.window = 2;
    lc.windowSet(800, 600);
    same(settle(lc, m), {Act::DestroySurface, Act::CreateSurface, Act::StopVsync}, "a refused window surface stops the vsync there");
    check((lc.stateWord() & kStateSurfaceBad) != 0 && !lc.canDraw(), "reported bad, nothing drawn");
    lc.windowChanged(600, 800);
    same(settle(lc, m), {}, "a size change does not retry it");

    // Three losses with no frame between them: the context counts as refused.
    m.window = 3;
    lc.windowSet(800, 600);
    settle(lc, m);
    for (int i = 0; i < Lifecycle::kMaxLostInARow; ++i) {
        m.loseOnNextCurrent = i + 1 < Lifecycle::kMaxLostInARow;
        lc.contextLost();
        settle(lc, m);
    }
    check((lc.stateWord() & kStateRefused) != 0 && !m.context, "three losses in a row stop the rebuilding");
    lc.quit();
    m.quit = true;
    settle(lc, m);
    check(m.releases == 1 && !m.releasedWithGl && m.contextCreates == m.contextDestroys, "released without GL, all freed");

    // A frame drawn between losses keeps the stage going.
    Lifecycle again(Kind::View);
    Model n;
    n.window = 1;
    again.windowSet(800, 600);
    settle(again, n);
    for (int i = 0; i < 2 * Lifecycle::kMaxLostInARow; ++i) {
        again.contextLost();
        settle(again, n);
        again.frameDrawn();
    }
    check(n.context && (again.stateWord() & kStateRefused) == 0, "losses with frames between them rebuild every time");
}

void eventsBeforeTheWindow() {
    Lifecycle lc(Kind::View);
    Model m;
    lc.setMode(RunMode::Animated, 0);
    lc.resume();
    lc.windowChanged(10, 10);
    lc.windowGone();
    same(settle(lc, m), {}, "nothing without a window, whatever came first");
    m.window = 1;
    lc.windowSet(640, 480);
    same(settle(lc, m), {Act::CreateContext, Act::CreateSurface, Act::MakeCurrent, Act::ClientSurfaceCreated, Act::ClientSurfaceChanged, Act::StartVsync},
         "the window then builds and paces");
    lc.windowSet(0, 0);
    same(settle(lc, m), {Act::StopVsync, Act::ClientTrimSpares, Act::DestroySurface}, "an empty window draws nothing");
}

// ---- every short sequence, and long random ones ----

enum Ev : int {
    kSetA, kSetB, kChange, kChangeEmpty, kGone, kAnimated, kStill, kStopped, kDark, kPause, kResume, kLost, kBad, kTick, kFrame,
    kFailContext, kFailSurface, kLoseCurrent, kRedraw, kQuit, kEvents
};

struct Run {
    Lifecycle lc;
    Model m;
    int64_t now = 0;
    int nextWindow = 1;
    explicit Run(Kind kind, bool surfaceless) : lc(kind) {
        m.surfacelessOk = surfaceless;
        lc.surfacelessSupported(surfaceless);
    }
};

void apply(Run& r, int ev) {
    r.now += 10;
    switch (ev) {
        case kSetA:
            r.m.window = r.nextWindow++;
            r.lc.windowSet(100, 200);
            break;
        case kSetB:
            r.m.window = r.nextWindow++;
            r.lc.windowSet(300, 100);
            break;
        case kChange:
            if (r.m.window) r.lc.windowChanged(50, 60);
            break;
        case kChangeEmpty:
            if (r.m.window) r.lc.windowChanged(0, 0);
            break;
        case kGone:
            r.m.window = 0;
            r.lc.windowGone();
            break;
        case kAnimated: r.lc.setMode(RunMode::Animated, r.now); break;
        case kStill: r.lc.setMode(RunMode::Still, r.now); break;
        case kStopped: r.lc.setMode(RunMode::Stopped, r.now); break;
        case kDark: r.lc.setMode(RunMode::Dark, r.now); break;
        case kPause: r.lc.pause(); break;
        case kResume: r.lc.resume(); break;
        case kLost: r.lc.contextLost(); break;
        case kBad: r.lc.surfaceBad(); break;
        case kTick:
            r.now += 3000;
            r.lc.tick(r.now);
            break;
        case kFrame:
            // A vsync's frame and its swap, the way the stage thread draws one.
            if (r.lc.vsyncOn() && r.lc.canDraw()) {
                if (!r.m.current || !r.m.clientBuilt) r.m.violate("a vsync frame without a current context");
                r.lc.frameDrawn();
            }
            break;
        case kFailContext: r.m.failNextContext = true; break;
        case kFailSurface: r.m.failNextSurface = true; break;
        case kLoseCurrent: r.m.loseOnNextCurrent = true; break;
        case kRedraw: r.lc.redrawNeeded(); break;
        case kQuit:
            r.lc.quit();
            r.m.quit = true;
            break;
        default: break;
    }
    // The window the shell holds goes to the model at once; the lifecycle hears of it in the same batch.
    settle(r.lc, r.m);
    invariants(r.lc, r.m);
}

void finish(Run& r) {
    apply(r, kQuit);
    // Events after quit change nothing.
    for (int ev = 0; ev < kEvents; ++ev) {
        if (ev == kSetA || ev == kSetB || ev == kGone) continue;  // the shell hands no window to a quit stage
        apply(r, ev);
    }
    if (r.m.releases != 1) r.m.violate("the client was not released exactly once");
    if (r.m.context || r.m.surfaceWindow != 0 || r.m.vsync || r.m.glCurrent()) r.m.violate("something outlived quit");
    if (r.m.contextCreates != r.m.contextDestroys) r.m.violate("a context create without its destroy");
    if (r.m.surfaceCreates != r.m.surfaceDestroys) r.m.violate("a surface create without its destroy");
    if (r.m.afterQuit != 0) r.m.violate("actions after quit had settled");
    if (!r.lc.finished()) r.m.violate("not finished after quit");
}

void exhaustive() {
    constexpr int kLength = 4;
    int sequences = 0;
    int bad = 0;
    for (const Kind kind : {Kind::View, Kind::Surface}) {
        int seq[kLength] = {};
        for (;;) {
            Run r(kind, kind == Kind::View);
            for (const int ev : seq) apply(r, ev);
            finish(r);
            ++sequences;
            if (r.m.violated) {
                if (bad++ < 5) {
                    std::fprintf(stderr, "FAIL: %s, kind %d, events", r.m.violation, static_cast<int>(kind));
                    for (const int ev : seq) std::fprintf(stderr, " %d", ev);
                    std::fprintf(stderr, "\n");
                }
            }
            int i = 0;
            while (i < kLength && ++seq[i] == kEvents) seq[i++] = 0;
            if (i == kLength) break;
        }
    }
    check(bad == 0, "every four-event sequence keeps the invariants");
    check(sequences == 2 * kEvents * kEvents * kEvents * kEvents, "and every one was run");
}

void randomLong() {
    uint32_t state = 0x9e3779b9u;
    auto rnd = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    int bad = 0;
    for (int run = 0; run < 4000; ++run) {
        const Kind kind = run % 2 == 0 ? Kind::View : Kind::Surface;
        Run r(kind, run % 3 != 0);
        const int length = 10 + static_cast<int>(rnd() % 40);
        for (int i = 0; i < length; ++i) {
            int ev = static_cast<int>(rnd() % kEvents);
            if (ev == kQuit && i + 1 < length) ev = kFrame;  // quit last, so the long runs test the middle
            apply(r, ev);
        }
        finish(r);
        if (r.m.violated && bad++ < 5) std::fprintf(stderr, "FAIL: %s in random run %d\n", r.m.violation, run);
    }
    check(bad == 0, "every long random run keeps the invariants");
}

}  // namespace

int main() {
    viewPaths();
    surfacePaths();
    refusedAndLost();
    eventsBeforeTheWindow();
    exhaustive();
    randomLong();
    if (failures) {
        std::fprintf(stderr, "%d stage lifecycle check(s) failed\n", failures);
        return 1;
    }
    std::printf("stage lifecycle: all checks passed\n");
    return 0;
}
