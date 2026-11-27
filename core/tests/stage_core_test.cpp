// Host-side checks for core/stage/StageCore: what a stage thread does with each command and on each turn of its
// loop. No EGL and no looper: a model port records every platform call, answers the pacer's times and can fail a swap
// the way a driver does, and a model client records what it is told.
//
// What is pinned: each command's lifecycle event, the window references, the hand-over's acknowledgement only after the
// old surface is gone, one paced frame per turn while the pacing runs and a frame can be drawn, a frame that swaps
// nothing (or a pacer that does not hold the swap) waiting for the vsync it asked for, the frame budget published from
// the pacer's times and following a display mode, the frame-rate vote restated for a new window, size and cap and never by
// a frame or a mode, the work reported against the budget as the draw alone, the presentation time each frame is drawn
// for, the client's service between frames, a lost context forgotten with none current, and a thread ending out of turn
// still releasing the client once and freeing everything.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "stage/FrameRecorder.hpp"
#include "stage/StageCore.hpp"

using namespace geode::stage;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

constexpr int64_t kPeriod60 = 16'666'667;
constexpr int64_t kPeriod90 = 11'111'111;

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
    bool pacing = false;
    FrameBudget paceBudget;
    int paceCalls = 0;
    int vsyncWakes = 0;
    int64_t applied = 0;
    bool surfaceAtAck = false;
    int64_t now = 100;
    int64_t swapCost = 0;
    int64_t gpuNs = 0;
    PaceTimes times{0, kPeriod60, true};
    std::vector<std::pair<int64_t, int64_t>> work;  // each frame's (target, actual) as reported for ADPF
    std::vector<std::string> recorded;

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
        now += swapCost;
        const Outcome out = nextSwap;
        nextSwap = Outcome::Done;
        return out;
    }
    void wakeAtVsync() override { vsyncWakes++; }
    void releaseWindow(void* window) override { released.push_back(window); }
    void setFrameRate(void* window, float fps) override {
        rateWindow = window;
        rate = fps;
        rateCalls++;
    }
    void pace(bool on, const FrameBudget& budget) override {
        pacing = on;
        paceBudget = budget;
        paceCalls++;
    }
    PaceTimes paceTimes() override { return times; }
    int64_t lastGpuNs() override { return gpuNs; }
    int64_t nowNs() override { return now; }
    void windowApplied(int64_t sequence) override {
        applied = sequence;
        surfaceAtAck = surfaceOn != nullptr;
    }
    void frameWork(int64_t targetNs, int64_t actualNs) override { work.emplace_back(targetNs, actualNs); }
    void framesRecorded(const std::string& csv) override { recorded.push_back(csv); }
};

struct Client final : StageClient {
    explicit Client(Port& p) : port(p) {}
    Port& port;
    int created = 0, changed = 0, frames = 0, trims = 0, losses = 0, releases = 0, services = 0;
    int width = 0, height = 0;
    int restarts = 0;
    float paced = -1.0f, cap = -1.0f;
    uint32_t nextEvents = 0;
    int64_t drawCost = 0;
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
    void framePresent(int64_t presentNs) override { presentTimes.push_back(presentNs); }
    uint32_t frame(float pacedFps, float capFps) override {
        if (frames == 0) appliedAtFirstFrame = port.applied;
        frames++;
        paced = pacedFps;
        cap = capFps;
        port.now += drawCost;
        return nextEvents;
    }
    void restartClock() override { restarts++; }
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
    // A watched, drawing view on window A: the turn that starts the pacing draws its first frame.
    void show() {
        core.apply(command(kStageWindow, 1080, 2340, 0.0f, 0.0f, 1, &windowA), now);
        core.apply(command(kStageMode, static_cast<int32_t>(RunMode::Animated)), now);
        core.step(now);
    }
    void turns(int n) {
        for (int i = 0; i < n; ++i) core.step(now);
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
    r.turns(3);
    check(r.client->frames == 0 && r.port.swaps == 0, "a hidden stage draws no frame");

    r.client->nextEvents = 0x4;
    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Animated)));
    check(r.port.pacing && r.port.paceBudget.swapNs == swapIntervalNs(60.0f), "a watched view starts the pacing with the cap's swap interval");
    check(r.client->frames == 1 && r.port.swaps == 1, "and its first turn draws a frame and swaps it");
    r.client->nextEvents = 0x10;
    r.turns(2);
    check(r.client->frames == 3 && r.port.swaps == 3, "each turn after draws one frame and swaps it: the swap does the waiting");
    check(near(r.client->paced, 60.0f) && r.client->cap == 60.0f, "told the rate the pacer holds and the user's cap");
    check(r.core.takeEvents() == 0x14 && r.core.takeEvents() == 0, "the frames' events add up and are taken once");
    check(r.core.waitMs(0) == 0, "the loop does not sleep while frames are due");

    const int rates = r.port.rateCalls;
    r.send(command(kStageSize, 2340, 1080));
    check(r.client->changed == 2 && r.client->width == 2340 && r.port.rateCalls == rates + 1, "a new size reaches the client and restates the rate");
    const int paces = r.port.paceCalls;
    r.send(command(kStageCap, 0, 0, 90.0f));
    check(r.port.rate == 90.0f && r.port.rateWindow == &windowA, "a cap goes to the window as its frame-rate vote");
    check(r.port.paceCalls == paces + 1 && r.port.paceBudget.swapNs == swapIntervalNs(90.0f) && r.port.paceBudget.voteFps == 90.0f, "and to the pacer once, in the one budget that holds the interval and the vote");
    const int hints = r.port.rateCalls;
    r.turns(5);
    check(r.port.rateCalls == hints, "a drawn frame states no rate: the vote is set on a change only");

    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Stopped)));
    check(!r.port.pacing && r.client->trims == 1, "hidden: the pacing stops and the spares go");
    const int frames = r.client->frames;
    r.turns(3);
    check(r.client->frames == frames, "and no frame is drawn");
    r.send(command(kStageRedraw));
    check(r.client->frames == frames + 1 && r.client->cap == 90.0f, "a redraw while stopped draws one frame, with the cap");
}

// Red against: a resume that steps the picture by the whole pause (no restart when the pacing starts again), a frame
// nobody paced stepping by the time since the last, and a restart on every drawn frame (the step would always be 0).
void theClockRestartsWhereNoTimePassedOnScreen() {
    Rig r;
    r.show();
    check(r.client->restarts == 1, "the pacing starting restarts the clock once");
    r.turns(2);
    check(r.client->restarts == 1, "drawn frames do not");
    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Stopped)));
    r.send(command(kStageRedraw));
    check(r.client->restarts == 2, "a frame no pacer asked for restarts it");
    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Animated)));
    check(r.client->restarts == 3, "and so does the pacing starting again after the stop");
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

// Red against: a frame told the time it starts instead of the pacer's presentation time, a jitter in the pacer's time
// stepping the picture by more or less than whole refresh periods, a late frame stepping by a catch-up of nothing (two
// frames on one refresh) and a time already past used as it is.
void aFrameIsToldTheTimeItIsShown() {
    Rig r;
    r.port.times.desiredNs = 1'000'000'000;
    r.show();
    check(r.client->presentTimes.size() == 1 && r.client->presentTimes[0] == 1'000'000'000, "a frame is told when the pacer will show it");
    r.port.times.desiredNs = 1'000'000'000 + kPeriod60 + 300;
    r.turns(1);
    check(r.client->presentTimes.back() == 1'000'000'000 + kPeriod60, "a jitter of the pacer's time snaps to whole refresh periods");
    r.port.times.desiredNs = 1'000'000'000 + 3 * kPeriod60 + 40'000;
    r.turns(1);
    check(r.client->presentTimes.back() == 1'000'000'000 + 3 * kPeriod60, "a late frame steps by the periods it really took, once");
    r.port.times.desiredNs = 1'000'000'000;
    r.port.now = 50;
    r.turns(1);
    check(r.client->presentTimes.back() == 1'000'000'000 + 4 * kPeriod60, "a time at or before the last frame's is the next refresh: never two frames on one");
    r.port.times.desiredNs = 0;
    r.port.now = 9'000'000'000;
    r.turns(1);
    check(r.client->presentTimes.back() >= 9'000'000'000 + 2 * kPeriod60 - kPeriod60 / 2, "with no time from the pacer a frame is two frames ahead, as a pipelined one is");
}

// Red against: a frame that repeats the picture on screen still swapping (the GPU does the work of a present for
// nothing), its bit reaching the shell's events, and a stage that spins on a frame nothing waits for.
void aFrameThatRepeatsThePictureIsNotSwapped() {
    Rig r;
    r.show();
    const int swaps = r.port.swaps;
    check(r.port.vsyncWakes == 0, "a frame whose swap held it asks for no vsync");
    r.client->nextEvents = kFrameNothingNew | 0x40u;
    r.turns(1);
    check(r.client->frames == 2 && r.port.swaps == swaps, "a frame that repeats the picture is not swapped");
    check(r.port.vsyncWakes == 1 && r.core.waitMs(0) == -1, "no swap holds the next one back, so it asks for the vsync and the loop sleeps");
    check(r.core.takeEvents() == 0x40u, "its own bit never reaches the shell, the rest does");
    r.turns(4);
    check(r.client->frames == 2 && r.port.vsyncWakes == 1, "and no turn draws before the vsync, nor asks again");
    r.client->nextEvents = 0;
    r.send(command(kStageVsync));
    check(r.client->frames == 3 && r.port.swaps == swaps + 1 && r.core.waitMs(0) == 0, "the vsync makes it the next frame's turn, and a frame with a change swaps");
}

// Red against: a stage without Swappy drawing as fast as its swap lets it (a burst after a late frame), a frame per
// vsync lost or doubled, and a presentation time that is not the vsync's own.
void withoutSwappyTheVsyncPacesEachFrame() {
    Rig r;
    r.port.times = PaceTimes{2'000'000'000, kPeriod60, false};
    r.show();
    check(r.client->frames == 1 && r.port.swaps == 1 && r.port.vsyncWakes == 1, "a swapped frame no pacer holds asks for the next vsync");
    check(r.client->presentTimes.back() == 2'000'000'000, "told the vsync's presentation time");
    r.turns(5);
    check(r.client->frames == 1 && r.port.vsyncWakes == 1, "however many turns come before it, no second frame");
    r.port.times.desiredNs = 2'000'000'000 + kPeriod60;
    r.send(command(kStageVsync));
    check(r.client->frames == 2 && r.port.vsyncWakes == 2, "each vsync draws exactly one frame and asks for the next");
    r.port.times.desiredNs = 2'000'000'000 + 4 * kPeriod60;
    r.port.now = 2'000'000'000 + 3 * kPeriod60;
    r.send(command(kStageVsync));
    r.turns(3);
    check(r.client->frames == 3 && r.client->presentTimes.back() == 2'000'000'000 + 4 * kPeriod60, "a late frame is shown at the vsync it caught, with no catch-up frames after it");
    check(r.port.work.back().first == kPeriod60, "its budget is the display's period: nothing holds a longer frame");
}

// Red against: a budget that stays the number it started as when the display mode changes (the work target would
// be a 60 Hz frame on a 90 Hz panel), a vote restated for a mode (the vote is the setting's, whatever the platform
// does), a budget not told to the UI's getter, and work reported as the draw plus the swap's wait.
void theFrameBudgetFollowsTheModeAndTheWorkIsTheDraw() {
    Rig r;
    check(r.core.frameBudgetNs() == 0, "no budget before the pacer says");
    r.client->drawCost = 3'000'000;
    r.port.swapCost = 12'000'000;
    r.show();
    check(r.core.frameBudgetNs() == kPeriod60, "the budget is the setting on the mode the platform reports");
    check(r.port.paceBudget.budgetNs == r.core.frameBudgetNs() && r.port.paceBudget.periodNs == kPeriod60, "and the mirror is the descriptor the pacer was handed, not a second working-out");
    check(r.port.work.size() == 1 && r.port.work[0].first == kPeriod60 && r.port.work[0].second == 3'000'000,
          "the work is reported against the budget as the draw alone, not the swap's wait");
    const int hints = r.port.rateCalls;
    const float vote = r.port.rate;
    r.port.times.periodNs = kPeriod90;
    r.turns(1);
    check(r.core.frameBudgetNs() == 2 * kPeriod90 && r.port.work.back().first == 2 * kPeriod90, "a new display mode is a new budget, for the UI and for the work target");
    check(near(r.client->paced, 45.0f), "and the rate the frame is told");
    check(r.port.rateCalls == hints && r.port.rate == vote, "the vote does not follow the mode");
    r.send(command(kStageCap, 0, 0, -1.0f));
    check(r.port.rate == 0.0f && r.port.paceBudget.swapNs == 0 && r.port.paceBudget.voteFps == 0.0f, "Follow device votes no rate and hands the pacer no interval");
    r.turns(1);
    check(r.core.frameBudgetNs() == kPeriod90, "and its budget is one refresh period of the mode the system runs");
}

void pauseAndLoss() {
    Rig r;
    r.show();
    r.send(command(kStagePause, 1));
    check(r.port.surfaceOn == nullptr && r.port.context && !r.port.pacing && (r.core.state() & kStatePaused) != 0, "a pause lets the surface go and keeps the context");
    const int frames = r.client->frames;
    r.turns(2);
    check(r.client->frames == frames, "a paused stage draws nothing");
    r.send(command(kStagePause, 0));
    check(r.port.surfaceOn == &windowA && r.port.pacing && r.client->created == 1, "a resume rebuilds the surface, not the client");

    r.port.nextSwap = Outcome::ContextLost;
    r.turns(1);
    check(r.client->losses == 1 && !r.client->lostWhileCurrent, "a lost context is forgotten with none current");
    check(r.port.contextCreates == 2 && r.port.contextDestroys == 1 && r.client->created == 2 && r.port.current, "and rebuilt in the same turn");

    check(r.core.waitMs(0) == -1 && r.port.vsyncWakes == 1, "a frame that swapped nothing waits for the vsync, whatever the reason");
    r.send(command(kStageVsync));
    r.port.nextSwap = Outcome::BadSurface;
    r.turns(1);
    check((r.core.state() & kStateSurfaceBad) != 0 && r.port.surfaceOn == nullptr && !r.port.pacing, "a bad surface is destroyed and reported, the pacing stopped");
}

void serviceAndQuit() {
    Rig r;
    r.send(command(kStageService));
    check(r.client->services == 1 && !r.client->serviceReady, "service with nothing built says not ready");
    r.show();
    const int frames = r.client->frames;
    r.core.apply(command(kStageService), r.now);
    r.core.apply(command(kStageService), r.now);
    r.core.step(r.now);
    check(r.client->services == 2 && r.client->serviceReady && r.client->framesAtService == frames && r.client->frames == frames + 1,
          "service runs once, ready, before the turn's frame");
    r.core.step(r.now);
    check(r.client->services == 2, "and not again unasked");

    std::weak_ptr<Client> held = r.client;
    r.client.reset();
    check(!held.expired(), "the stage holds its client");
    const int swaps = r.port.swaps;
    r.send(command(kStageQuit));
    check(held.expired(), "and drops it at release");
    check(r.core.finished() && !r.port.context && r.port.surfaceOn == nullptr && !r.port.pacing, "quit frees everything");
    check(r.port.contextCreates == r.port.contextDestroys && r.port.surfaceCreates == r.port.surfaceDestroys, "every create has its destroy");
    check((r.core.state() & kStateReleased) != 0, "and reports released");
    r.send(command(kStageVsync));
    r.send(command(kStageService));
    check(r.port.swaps == swaps, "nothing draws after quit");
}

// Red against: a recorder that runs without its debug flag, a draw time that includes the swap's wait, a present
// interval read from anything but the presentation times, the pacer's GPU time not joining the frame before, an interval
// across a stop, and a recording kept back when the pacing stops (nothing else would hand it over).
void theFrameRecorderIsOffUntilAskedAndHandsOverAtTheStop() {
    Rig off;
    off.show();
    off.turns(4);
    off.send(command(kStageMode, static_cast<int32_t>(RunMode::Stopped)));
    check(off.port.recorded.empty(), "with no debug flag nothing is recorded or handed over");

    Rig r;
    r.core.recordFrames(true);
    r.client->drawCost = 4'000'000;
    r.port.swapCost = 9'000'000;
    r.port.gpuNs = 5'000'000;
    r.port.times.desiredNs = 1'000'000'000;
    r.show();
    r.port.times.desiredNs = 1'000'000'000 + kPeriod60;
    r.turns(1);
    r.port.times.desiredNs = 1'000'000'000 + 2 * kPeriod60;
    r.port.gpuNs = 0;
    r.turns(1);
    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Stopped)));
    check(r.port.recorded.size() == 1, "the pacing stopping hands the recording over once");
    const std::string csv = r.port.recorded.empty() ? std::string() : r.port.recorded[0];
    const std::string header = FrameRecorder::kCsvHeader;
    check(csv.compare(0, header.size(), header) == 0, "as the recorder's CSV");
    check(csv.find("\n0,4000000,5000000,0\n") != std::string::npos, "the first frame: its draw alone, its GPU time, no interval across the start");
    check(csv.find("\n1,4000000,0,16666667\n") != std::string::npos || csv.find("\n1,4000000,5000000,16666667\n") != std::string::npos,
          "the next: its draw and the presentation-to-presentation interval");
    check(csv.find("\n2,4000000,0,16666667\n") != std::string::npos, "and the third, its GPU time not yet told");

    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Animated)));
    r.send(command(kStageMode, static_cast<int32_t>(RunMode::Stopped)));
    check(r.port.recorded.size() == 2 && r.port.recorded[1].find("\n3,4000000,") != std::string::npos &&
              r.port.recorded[1].find(",0\n") != std::string::npos,
          "the numbering goes on, and the first frame after a start has no interval across the stop");
}

// Red against: the user's rate stated on every frame (a call across to the system per frame), a stated rate that is the
// display's own, and Follow device stated as a rate.
void theRateVoteFollowsTheCapNotTheFrames() {
    Rig r(Kind::View, 60.0f);
    r.show();
    check(r.port.rate == 60.0f, "the window is voted the user's cap");
    r.send(command(kStageCap, 0, 0, -1.0f));
    check(r.port.rate == 0.0f, "Follow device states no rate");
    r.send(command(kStageCap, 0, 0, 30.0f));
    check(r.port.rate == 30.0f, "a new cap is stated");
    const int hints = r.port.rateCalls;
    r.turns(20);
    check(r.port.rateCalls == hints, "twenty frames state nothing");
    r.send(command(kStageCap, 0, 0, 30.0f));
    check(r.port.rateCalls == hints, "the same cap again states nothing");
}

// Red against: the acknowledgement sent after the client's surface calls or the first frame, which keeps the shell's
// main thread waiting on shader warm-up or a blocked swap.
void aWindowIsAcknowledgedBeforeTheClientOrAFrame() {
    Rig r;
    r.core.apply(command(kStageWindow, 1080, 2340, 0.0f, 0.0f, 1, &windowA), r.now);
    r.core.apply(command(kStageMode, static_cast<int32_t>(RunMode::Animated)), r.now);
    r.core.step(r.now);
    check(r.client->created == 1 && r.client->appliedAtCreated == 1, "a new window is acknowledged before the client is told of it");
    check(r.port.surfaceAtAck, "with its surface already made");
    check(r.client->frames == 1 && r.client->appliedAtFirstFrame == 1, "and before the first frame");

    r.send(command(kStageWindowGone, 0, 0, 0.0f, 0.0f, 2));
    check(r.port.applied == 2 && !r.port.surfaceAtAck, "a window that went is acknowledged with its surface gone");
}

void endsOutOfTurn() {
    Rig r;
    r.show();
    r.core.abandon();
    check(r.client->releases == 1 && r.client->releasedWithGl, "a thread ending out of turn still releases the client, with its context current");
    check(r.core.finished() && !r.port.context && r.port.surfaceOn == nullptr && !r.port.pacing, "and frees the surface, the context and the pacing");
    r.core.abandon();
    check(r.client->releases == 1, "once");

    Rig bare;
    bare.core.abandon();
    check(bare.client->releases == 1 && !bare.client->releasedWithGl && bare.core.finished(), "a stage that never built releases without GL");
}

}  // namespace

int main() {
    windowAndFrames();
    theClockRestartsWhereNoTimePassedOnScreen();
    trimReachesAStageThatDrawsNothing();
    handOvers();
    aFrameIsToldTheTimeItIsShown();
    aFrameThatRepeatsThePictureIsNotSwapped();
    withoutSwappyTheVsyncPacesEachFrame();
    theFrameBudgetFollowsTheModeAndTheWorkIsTheDraw();
    pauseAndLoss();
    serviceAndQuit();
    theRateVoteFollowsTheCapNotTheFrames();
    theFrameRecorderIsOffUntilAskedAndHandsOverAtTheStop();
    aWindowIsAcknowledgedBeforeTheClientOrAFrame();
    endsOutOfTurn();
    if (failures) {
        std::fprintf(stderr, "%d stage core check(s) failed\n", failures);
        return 1;
    }
    std::printf("stage core: all checks passed\n");
    return 0;
}
