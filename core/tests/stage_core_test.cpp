// Host-side checks for core/stage/StageCore: what a stage thread does with each command and on each turn of its
// loop (docs/agents/native-stage.md). No EGL and no looper: a model port records every platform call and can fail
// a swap the way a driver does, and a model client records what it is told.
//
// What is pinned: each command's lifecycle event (window, window gone, size, mode, pause and resume, redraw, cap,
// quit), the window references (one release per hand-over, replaced, gone, discarded or closed), the hand-over's
// acknowledgement only after the old surface is gone, a vsync's frame drawn only while the vsync runs and a frame
// can be drawn, the frame-rate preference restated for a new window, a new size and a new cap and never by a
// frame, a vsync skipped while the GPU has not finished the last frame (and drawn again the moment it has), the
// presentation rate read from the vsync's interval, the client's service between frames, a lost context forgotten with none current, and a thread ending out of turn still
// releasing the client once and freeing everything.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "stage/StageCore.hpp"
#include "stage/VsyncRule.hpp"

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
    bool gpuBusyNow = false;  // the last swapped frame's fence has not signalled
    int gpuAsks = 0;
    int64_t applied = 0;
    bool surfaceAtAck = false;
    int frameDones = 0;
    std::vector<int64_t> presented;  // each swap's stamp, 0 for none

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
    void presentAt(int64_t presentNs) override { presented.push_back(presentNs); }
    void frameDone() override { frameDones++; }
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
    bool gpuBusy() override {
        gpuAsks++;
        return gpuBusyNow;
    }
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
    float governed = 0.0f;  // the cap the governor asks for after each frame, 0 for none
    uint32_t nextEvents = 0;
    std::vector<int64_t> vsyncTimes;  // the frame time each frame was told, 0 for none
    std::vector<int64_t> presentTimes;  // the presentation time each frame was told, 0 for none
    bool lostWhileCurrent = false;
    bool releasedWithGl = false;
    bool serviceReady = false;
    int framesAtService = -1;

    int64_t appliedAtCreated = -1;
    int64_t appliedAtFirstFrame = -1;
    void surfaceCreated() override {
        created++;
        appliedAtCreated = port.applied;
    }
    void surfaceChanged(int w, int h) override {
        changed++;
        width = w;
        height = h;
    }
    void frameTime(int64_t vsyncNs) override { vsyncTimes.push_back(vsyncNs); }
    void framePresent(int64_t presentNs) override { presentTimes.push_back(presentNs); }
    uint32_t frame(float pacedFps, float capFps) override {
        if (frames == 0) appliedAtFirstFrame = port.applied;
        frames++;
        paced = pacedFps;
        cap = capFps;
        return nextEvents;
    }
    float governedCapFps() override { return governed; }
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

bool near(float a, float b) { return a - b < 0.01f && b - a < 0.01f; }

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
    r.send(command(kStageWindow, 1080, 2340, 0.0f, 0.0f, 1, &windowA));
    check(r.port.context && r.port.surfaceOn == &windowA && r.port.current, "a window builds the context and a surface on it");
    check(r.client->created == 1 && r.client->changed == 1 && r.client->width == 1080 && r.client->height == 2340, "and tells the client its size");
    check(r.port.applied == 1, "then the hand-over is acknowledged");
    check(r.port.rateWindow == &windowA && r.port.rate == 60.0f, "a new window is told the cap's rate");
    check((r.core.state() & kStateDrawable) != 0 && (r.core.state() & kStateVsync) == 0, "drawable, not pacing while hidden");

    r.send(command(kStageFrameDue, 33'333'333, 0));
    check(r.client->frames == 0 && r.port.swaps == 0, "a frame request with the vsync stopped is dropped");
    check(near(r.core.pacedFps(), 30.0f), "its rate, read from the interval between presentations, is still read back");

    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Animated)));
    check(r.port.vsync, "a watched view starts the vsync");
    r.client->nextEvents = 0x4;
    r.send(command(kStageFrameDue, 22'222'222, 0));
    r.client->nextEvents = 0x10;
    r.send(command(kStageFrameDue, 22'222'222, 0));
    check(r.client->frames == 2 && r.port.swaps == 2, "each request draws one frame and swaps it");
    check(near(r.client->paced, 45.0f) && r.client->cap == 60.0f, "told the rate the display presents at and the user's cap");
    check(r.core.takeEvents() == 0x14 && r.core.takeEvents() == 0, "the frames' events add up and are taken once");

    r.core.step(r.now);
    check(r.client->frames == 2, "no frame without a request");

    const int rates = r.port.rateCalls;
    r.send(command(kStageSize, 2340, 1080));
    check(r.client->changed == 2 && r.client->width == 2340 && r.port.rateCalls == rates + 1, "a new size reaches the client and restates the rate");
    r.send(command(kStageCap, 0, 0, 90.0f));
    check(r.port.rate == 90.0f && r.port.rateWindow == &windowA, "a cap goes to the window as its frame-rate hint");
    check(near(r.core.pacedFps(), 45.0f), "and does not move the rate the display presents at");
    const int hints = r.port.rateCalls;
    r.send(command(kStageFrameDue, 22'222'222, 0));
    check(r.port.rateCalls == hints, "a drawn frame states no rate: the hint is set on a change only");

    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Stopped)));
    check(!r.port.vsync && r.client->trims == 1, "hidden: the vsync stops and the spares go");
    r.send(command(kStageRedraw));
    check(r.client->frames == 4 && near(r.client->paced, 45.0f) && r.client->cap == 90.0f, "a redraw while stopped draws one frame at the last rate, with the cap");
}

void trimReachesAStageThatDrawsNothing() {
    Rig r;
    r.show();
    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Still)));
    r.core.step(r.now + 5000);
    const int before = r.client->trims;
    r.send(command(kStageTrim));
    check(r.client->trims == before + 1, "a trim command makes the idle stage shed its spares");
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

void vsyncTimes() {
    Rig r;
    r.show();
    // t = the vsync's frame time, b = ns from it to the expected presentation, a = ns between presentations.
    r.send(command(kStageFrameDue, 8'000'000, 20'000'000, 60.0f, 60.0f, 1'000'000'000));
    check(r.client->vsyncTimes.size() == 1 && r.client->vsyncTimes[0] == 1'000'000'000, "a frame is told the vsync it is for");
    check(r.client->presentTimes.back() == 1'020'000'000, "and the vsync it is shown at");
    check(r.port.presented.size() == 1 && r.port.presented[0] == 1'016'000'000,
          "the swap is stamped half an interval before that vsync, between it and the one before");
    r.send(command(kStageFrameDue, 0, 20'000'000, 60.0f, 60.0f, 1'008'000'000));
    check(r.client->presentTimes.back() == 1'028'000'000 && r.port.presented.size() == 2 && r.port.presented[1] == 1'028'000'000,
          "with no interval the stamp is the presentation time itself, as it was");
    r.send(command(kStageFrameDue, 8'000'000, 20'000'000, 60.0f, 60.0f, 1'016'000'000));
    check(r.port.presented.size() == 3 && r.port.presented[2] == 1'032'000'000, "the next frame with one is stamped again");
    r.send(command(kStageFrameDue, 8'000'000, 0, 60.0f, 60.0f, 1'024'000'000));
    check(r.client->vsyncTimes.back() == 1'024'000'000 && r.port.presented.size() == 4 && r.port.presented[3] == 0,
          "a vsync with no expected time presents as it can");
    r.send(command(kStageFrameDue, 0, 0, 60.0f, 60.0f));
    check(r.client->vsyncTimes.back() == 0 && r.port.presented.back() == 0, "a request with no vsync time says so");
}

// Red against: a pacer never told the stage is free (frameDone not called, or called before the swap), a dropped
// request that leaves the pacer waiting for ever, and a client not told when its frame is shown (the sound is
// read there).
void aFrameIsToldItsPresentationTimeAndTheStageSaysWhenItIsDone() {
    Rig r;
    r.show();
    r.send(command(kStageFrameDue, 0, 33'000'000, 30.0f, 30.0f, 2'000'000'000));
    check(r.client->presentTimes.size() == 1 && r.client->presentTimes[0] == 2'033'000'000, "a frame is told when it is shown");
    check(r.port.frameDones == 1, "the pacer is told the frame is done, once");
    r.send(command(kStagePause, 1));
    const int before = r.port.frameDones;
    r.send(command(kStageFrameDue, 0, 0, 30.0f, 30.0f, 2'100'000'000));
    check(r.client->frames == 1 && r.port.frameDones == before + 1, "a request dropped unseen is done too, so the pacer is not left waiting");
}

// Red against: a frame the client says repeats the picture on screen still swapping (the GPU does the work of a
// present for nothing), its bit reaching the shell's events, and a skipped frame leaving the pacer waiting.
// Any change draws and swaps on the very next vsync.
void aFrameThatRepeatsThePictureIsNotSwapped() {
    Rig r;
    r.show();
    r.send(command(kStageFrameDue, 0, 4'000'000, 30.0f, 30.0f, 1'000'000'000));
    const int swaps = r.port.swaps;
    const size_t presents = r.port.presented.size();
    r.client->nextEvents = kFrameNothingNew | 0x40u;
    r.send(command(kStageFrameDue, 0, 4'000'000, 30.0f, 30.0f, 1'033'000'000));
    check(r.client->frames == 2 && r.port.swaps == swaps && r.port.presented.size() == presents,
          "a frame that repeats the picture is neither presented nor swapped");
    check(r.port.frameDones == 2, "and the pacer is told it is done");
    check(r.core.takeEvents() == 0x40u, "its own bit never reaches the shell, the rest does");
    r.client->nextEvents = 0;
    r.send(command(kStageFrameDue, 0, 4'000'000, 30.0f, 30.0f, 1'066'000'000));
    check(r.port.swaps == swaps + 1 && r.port.presented.size() == presents + 1, "the next frame with a change swaps");
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

// Red against: a vsync drawn on top of a frame the GPU has not finished (the queue stuffs and every frame lands late),
// a skip that is remembered (the next vsync waits too), a skipped request left unanswered (the vsync thread would
// wait for ever), and a fence asked about on a vsync nothing will draw.
void aVsyncIsSkippedWhileTheGpuHasNotFinishedTheLastFrame() {
    Rig r;
    r.show();
    r.send(command(kStageFrameDue, 16'666'667, 0));
    check(r.client->frames == 1 && r.port.swaps == 1, "an idle GPU draws");
    r.port.gpuBusyNow = true;
    r.send(command(kStageFrameDue, 16'666'667, 0));
    r.send(command(kStageFrameDue, 16'666'667, 0));
    check(r.client->frames == 1 && r.port.swaps == 1, "a GPU still on the last frame skips the vsync, however many come");
    check(r.port.frameDones == 3, "and the vsync thread is told each request is done");
    r.port.gpuBusyNow = false;
    r.send(command(kStageFrameDue, 16'666'667, 0));
    check(r.client->frames == 2 && r.port.swaps == 2, "the first vsync after the fence signals draws: no skip is remembered");

    r.send(command(kStagePause, 1));
    const int asks = r.port.gpuAsks;
    r.send(command(kStageFrameDue, 16'666'667, 0));
    check(r.port.gpuAsks == asks, "a request that will not draw never asks the fence");
}

// Red against: the user's rate stated on every frame (a call across to the system per vsync), a stated rate that is
// the display's own, and Follow device stated as a rate.
void theRateHintFollowsTheCapNotTheFrames() {
    Rig r(Kind::View, 60.0f);
    r.show();
    check(r.port.rate == 60.0f, "the window is asked for the user's cap");
    r.send(command(kStageCap, 0, 0, -1.0f));
    check(r.port.rate == 0.0f, "Follow device states no rate");
    r.send(command(kStageCap, 0, 0, 30.0f));
    check(r.port.rate == 30.0f, "a new cap is stated");
    const int hints = r.port.rateCalls;
    for (int i = 0; i < 20; ++i) r.send(command(kStageFrameDue, 8'333'333, 0));
    check(r.port.rateCalls == hints && r.client->frames == 20, "twenty frames at a faster display state nothing and are all drawn");
}

// Red against: the acknowledgement sent after the client's surface calls or the first frame, which keeps the
// shell's Main thread waiting on shader warm-up or a blocked swap.
void aWindowIsAcknowledgedBeforeTheClientOrAFrame() {
    Rig r;
    r.core.apply(command(kStageWindow, 1080, 2340, 0.0f, 0.0f, 1, &windowA), r.now);
    r.core.apply(command(kStageMode, static_cast<int32_t>(RunMode::Animated)), r.now);
    r.core.step(r.now);
    check(r.client->created == 1 && r.client->appliedAtCreated == 1, "a new window is acknowledged before the client is told of it");
    check(r.port.surfaceAtAck, "with its surface already made");
    r.send(command(kStageFrameDue, 0, 0, 60.0f, 60.0f));
    check(r.client->frames == 1 && r.client->appliedAtFirstFrame == 1, "and before the first frame");

    r.send(command(kStageWindowGone, 0, 0, 0.0f, 0.0f, 2));
    check(r.port.applied == 2 && !r.port.surfaceAtAck, "a window that went is acknowledged with its surface gone");
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
    trimReachesAStageThatDrawsNothing();
    handOvers();
    vsyncTimes();
    aFrameIsToldItsPresentationTimeAndTheStageSaysWhenItIsDone();
    aFrameThatRepeatsThePictureIsNotSwapped();
    pauseAndLoss();
    serviceAndQuit();
    aVsyncIsSkippedWhileTheGpuHasNotFinishedTheLastFrame();
    theRateHintFollowsTheCapNotTheFrames();
    aWindowIsAcknowledgedBeforeTheClientOrAFrame();
    endsOutOfTurn();
    if (failures) {
        std::fprintf(stderr, "%d stage core check(s) failed\n", failures);
        return 1;
    }
    std::printf("stage core: all checks passed\n");
    return 0;
}
