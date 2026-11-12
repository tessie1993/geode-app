// Host-side checks for core/stage/StageCore: what a stage thread does with each command and on each turn of its
// loop (docs/agents/native-stage.md). No EGL and no looper: a model port records every platform call and can fail
// a swap the way a driver does, and a model client records what it is told.
//
// What is pinned: each command's lifecycle event (window, window gone, size, mode, pause and resume, redraw, cap,
// quit), the window references (one release per hand-over, replaced, gone, discarded or closed), the hand-over's
// acknowledgement only after the old surface is gone, a vsync's frame drawn only while the vsync runs and a frame
// can be drawn, the frame-rate preference restated for a new window, a new size and a new cap, the client's
// service between frames, a lost context forgotten with none current, and a thread ending out of turn still
// releasing the client once and freeing everything.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "stage/StageCore.hpp"
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

struct Port final : StagePort {
    std::vector<std::string> log;
    bool context = false;
    void* surfaceOn = nullptr;
    bool current = false;
    int contextCreates = 0, contextDestroys = 0, surfaceCreates = 0, surfaceDestroys = 0;
    Outcome nextSwap = Outcome::Done;
    int swaps = 0;
    std::vector<void*> released;
    void* rateWindow = nullptr;
    float rate = -1.0f;
    int rateCalls = 0;
    bool vsync = false;
    float vsyncCapFps = -1.0f;
    int rungs = 0;
    int64_t applied = 0;
    bool surfaceAtAck = false;

    Outcome createContext(bool& surfaceless) override {
        surfaceless = true;
        context = true;
        contextCreates++;
        log.push_back("context");
        return Outcome::Done;
    }
    Outcome createSurface(void* window) override {
        surfaceOn = window;
        surfaceCreates++;
        log.push_back("surface");
        return Outcome::Done;
    }
    Outcome makeCurrent() override {
        current = true;
        log.push_back("current");
        return Outcome::Done;
    }
    Outcome makeCurrentSurfaceless() override {
        current = true;
        log.push_back("surfaceless");
        return Outcome::Done;
    }
    void releaseCurrent() override {
        current = false;
        log.push_back("uncurrent");
    }
    void destroySurface() override {
        surfaceOn = nullptr;
        current = false;
        surfaceDestroys++;
        log.push_back("-surface");
    }
    void destroyContext() override {
        context = false;
        current = false;
        contextDestroys++;
        log.push_back("-context");
    }
    Outcome swap() override {
        swaps++;
        const Outcome out = nextSwap;
        nextSwap = Outcome::Done;
        return out;
    }
    void releaseWindow(void* window) override { released.push_back(window); }
    void setFrameRate(void* window, float fps) override {
        rateWindow = window;
        rate = fps;
        rateCalls++;
    }
    void vsyncRun(bool on) override { vsync = on; }
    void vsyncCap(float capFps) override { vsyncCapFps = capFps; }
    int fpsRungsDown() override { return rungs; }
    void windowApplied(int64_t sequence) override {
        applied = sequence;
        surfaceAtAck = surfaceOn != nullptr;
    }
};

struct Client final : StageClient {
    explicit Client(Port& p) : port(p) {}
    Port& port;
    int created = 0, changed = 0, frames = 0, trims = 0, losses = 0, releases = 0, services = 0;
    int width = 0, height = 0;
    float paced = -1.0f, cap = -1.0f;
    uint32_t nextEvents = 0;
    bool lostWhileCurrent = false;
    bool releasedWithGl = false;
    bool serviceReady = false;
    int framesAtService = -1;

    void surfaceCreated() override { created++; }
    void surfaceChanged(int w, int h) override {
        changed++;
        width = w;
        height = h;
    }
    uint32_t frame(float pacedFps, float capFps) override {
        frames++;
        paced = pacedFps;
        cap = capFps;
        return nextEvents;
    }
    void trimSpares() override { trims++; }
    void contextLost() override {
        losses++;
        lostWhileCurrent = lostWhileCurrent || port.current;
    }
    void service(bool ready) override {
        services++;
        serviceReady = ready;
        framesAtService = frames;
    }
    void release(bool glCurrent) override {
        releases++;
        releasedWithGl = glCurrent;
    }
};

Command command(uint16_t kind, int32_t a = 0, int32_t b = 0, float f = 0.0f, float g = 0.0f, int64_t t = 0, void* target = nullptr) {
    Command c;
    c.kind = kind;
    c.a = a;
    c.b = b;
    c.f = f;
    c.g = g;
    c.t = t;
    c.target = target;
    return c;
}

int windowA = 0, windowB = 0;

struct Rig {
    Port port;
    std::shared_ptr<Client> client = std::make_shared<Client>(port);
    StageCore core;
    int64_t now = 0;
    explicit Rig(Kind kind = Kind::View, float cap = 60.0f) : core(kind, client, cap, port) {}
    void send(const Command& c) {
        core.apply(c, now);
        core.step(now);
    }
    // A watched, drawing view on window A.
    void show() {
        core.apply(command(kStageWindow, 1080, 2340, 0.0f, 0.0f, 1, &windowA), now);
        core.apply(command(kStageMode, static_cast<int32_t>(RunMode::Animated)), now);
        core.step(now);
    }
};

void windowAndFrames() {
    Rig r;
    r.port.rungs = 1;
    r.send(command(kStageWindow, 1080, 2340, 0.0f, 0.0f, 1, &windowA));
    check(r.port.context && r.port.surfaceOn == &windowA && r.port.current, "a window builds the context and a surface on it");
    check(r.client->created == 1 && r.client->changed == 1 && r.client->width == 1080 && r.client->height == 2340, "and tells the client its size");
    check(r.port.applied == 1, "then the hand-over is acknowledged");
    check(r.port.rateWindow == &windowA && r.port.rate == StagePacing::frameRateFor(60.0f, 1), "a new window is told the cap's rate at the thermal rung");
    check((r.core.state() & kStateDrawable) != 0 && (r.core.state() & kStateVsync) == 0, "drawable, not pacing while hidden");

    r.send(command(kStageFrameDue, 0, 0, 30.0f, 60.0f));
    check(r.client->frames == 0 && r.port.swaps == 0, "a frame request with the vsync stopped is dropped");
    check(r.core.pacedFps() == 30.0f, "its rate is still the one read back");

    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Animated)));
    check(r.port.vsync, "a watched view starts the vsync");
    r.client->nextEvents = 0x4;
    r.send(command(kStageFrameDue, 0, 0, 45.0f, 60.0f));
    r.client->nextEvents = 0x10;
    r.send(command(kStageFrameDue, 0, 0, 45.0f, 60.0f));
    check(r.client->frames == 2 && r.port.swaps == 2, "each request draws one frame and swaps it");
    check(r.client->paced == 45.0f && r.client->cap == 60.0f, "at the pacer's rates");
    check(r.core.takeEvents() == 0x14 && r.core.takeEvents() == 0, "the frames' events add up and are taken once");

    r.core.step(r.now);
    check(r.client->frames == 2, "no frame without a request");

    const int rates = r.port.rateCalls;
    r.send(command(kStageSize, 2340, 1080));
    check(r.client->changed == 2 && r.client->width == 2340 && r.port.rateCalls == rates + 1, "a new size reaches the client and restates the rate");
    r.send(command(kStageCap, 0, 0, 90.0f));
    check(r.port.vsyncCapFps == 90.0f && r.port.rate == StagePacing::frameRateFor(90.0f, 1) && r.core.pacedFps() == 90.0f,
          "a cap goes to the pacer, the window and the read-back");
    r.send(command(kStageFrameRate, 0, 0, 72.0f));
    check(r.port.rate == 72.0f && r.port.rateWindow == &windowA, "the pacer's hint goes to the window");

    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Stopped)));
    check(!r.port.vsync && r.client->trims == 1, "hidden: the vsync stops and the spares go");
    r.send(command(kStageRedraw));
    check(r.client->frames == 3 && r.client->paced == 90.0f && r.client->cap == 90.0f, "a redraw while stopped draws one frame at the cap");
}

void handOvers() {
    Rig r;
    r.show();
    r.send(command(kStageWindow, 800, 600, 0.0f, 0.0f, 2, &windowB));
    check(r.port.released.size() == 1 && r.port.released[0] == &windowA, "a replaced window's reference is released once");
    check(r.port.surfaceOn == &windowB && r.port.surfaceDestroys == 1 && r.port.applied == 2, "the surface moves to the new window before the acknowledgement");

    r.send(command(kStageWindowGone, 0, 0, 0.0f, 0.0f, 3));
    check(r.port.released.size() == 2 && r.port.released[1] == &windowB, "a gone window's reference is released");
    check(r.port.applied == 3 && !r.port.surfaceAtAck, "and acknowledged only once no surface is left on it");
    check(r.port.context, "the context outlives the window");
    const int rates = r.port.rateCalls;
    r.send(command(kStageFrameRate, 0, 0, 72.0f));
    r.send(command(kStageCap, 0, 0, 90.0f));
    check(r.port.rateCalls == rates, "no rate is stated with no window");

    // Two hand-overs in one batch: one acknowledgement, of the later one.
    r.core.apply(command(kStageWindow, 800, 600, 0.0f, 0.0f, 4, &windowA), r.now);
    r.core.apply(command(kStageWindow, 800, 600, 0.0f, 0.0f, 5, &windowB), r.now);
    r.core.step(r.now);
    check(r.port.applied == 5 && r.port.surfaceOn == &windowB && r.port.released.size() == 3, "a batch of hand-overs settles on the last");

    r.core.discard(command(kStageWindow, 1, 1, 0.0f, 0.0f, 6, &windowA));
    r.core.discard(command(kStageSize, 1, 1));
    check(r.port.released.size() == 4 && r.port.released[3] == &windowA, "a hand-over never applied gives its window back, other commands nothing");
    r.core.close();
    r.core.close();
    check(r.port.released.size() == 5 && r.port.released[4] == &windowB, "closing releases the stage's own window once");
}

void pauseAndLoss() {
    Rig r;
    r.show();
    r.send(command(kStagePause, 1));
    check(r.port.surfaceOn == nullptr && r.port.context && !r.port.vsync && (r.core.state() & kStatePaused) != 0, "a pause lets the surface go and keeps the context");
    r.send(command(kStageFrameDue, 0, 0, 60.0f, 60.0f));
    check(r.client->frames == 0, "a frame request in flight across the pause is dropped");
    r.send(command(kStagePause, 0));
    check(r.port.surfaceOn == &windowA && r.port.vsync && r.client->created == 1, "a resume rebuilds the surface, not the client");

    r.port.nextSwap = Outcome::ContextLost;
    r.send(command(kStageFrameDue, 0, 0, 60.0f, 60.0f));
    check(r.client->losses == 1 && !r.client->lostWhileCurrent, "a lost context is forgotten with none current");
    check(r.port.contextCreates == 2 && r.port.contextDestroys == 1 && r.client->created == 2 && r.port.current, "and rebuilt in the same turn");

    r.port.nextSwap = Outcome::BadSurface;
    r.send(command(kStageFrameDue, 0, 0, 60.0f, 60.0f));
    check((r.core.state() & kStateSurfaceBad) != 0 && r.port.surfaceOn == nullptr && !r.port.vsync, "a bad surface is destroyed and reported, the vsync stopped");
}

void serviceAndQuit() {
    Rig r;
    r.send(command(kStageService));
    check(r.client->services == 1 && !r.client->serviceReady, "service with nothing built says not ready");
    r.show();
    r.core.apply(command(kStageService), r.now);
    r.core.apply(command(kStageService), r.now);
    r.core.apply(command(kStageFrameDue, 0, 0, 60.0f, 60.0f), r.now);
    r.core.step(r.now);
    check(r.client->services == 2 && r.client->serviceReady && r.client->framesAtService == 0 && r.client->frames == 1,
          "service runs once, ready, before the turn's frame");
    r.core.step(r.now);
    check(r.client->services == 2, "and not again unasked");

    std::weak_ptr<Client> held = r.client;
    r.client.reset();
    check(!held.expired(), "the stage holds its client");
    const int swaps = r.port.swaps;
    r.send(command(kStageQuit));
    check(held.expired(), "and drops it at release");
    check(r.core.finished() && !r.port.context && r.port.surfaceOn == nullptr && !r.port.vsync, "quit frees everything");
    check(r.port.contextCreates == r.port.contextDestroys && r.port.surfaceCreates == r.port.surfaceDestroys, "every create has its destroy");
    check((r.core.state() & kStateReleased) != 0, "and reports released");
    r.send(command(kStageFrameDue, 0, 0, 60.0f, 60.0f));
    r.send(command(kStageService));
    check(r.port.swaps == swaps, "nothing draws after quit");
}

void endsOutOfTurn() {
    Rig r;
    r.show();
    r.core.abandon();
    check(r.client->releases == 1 && r.client->releasedWithGl, "a thread ending out of turn still releases the client, with its context current");
    check(r.core.finished() && !r.port.context && r.port.surfaceOn == nullptr && !r.port.vsync, "and frees the surface, the context and the vsync");
    r.core.abandon();
    check(r.client->releases == 1, "once");

    Rig bare;
    bare.core.abandon();
    check(bare.client->releases == 1 && !bare.client->releasedWithGl && bare.core.finished(), "a stage that never built releases without GL");
}

}  // namespace

int main() {
    windowAndFrames();
    handOvers();
    pauseAndLoss();
    serviceAndQuit();
    endsOutOfTurn();
    if (failures) {
        std::fprintf(stderr, "%d stage core check(s) failed\n", failures);
        return 1;
    }
    std::printf("stage core: all checks passed\n");
    return 0;
}
