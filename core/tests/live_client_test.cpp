// Host-side checks for core/stage/LiveClient: what a live stage's client does between the stage thread and the
// threads that talk to it (docs/agents/native-stage.md). The renderer is a model that records every call.
//
// What is pinned: a frame hears the ring set last and none after it is cleared; what a frame reported is kept
// until polled (an error text only when it differs, director moments summed,
// the liquid mode, a still, asked for again when it could not be read); a transition's warm-up waits for a
// context; a tee opens on the stage thread, fails without a context, and closes exactly once, on its stop or on
// the stage's release; nothing is queued after the release, and a renderer released is no longer handed to other
// threads. A look's parts: core/tests/look_snapshot_test.cpp.
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "stage/LiveClient.hpp"

using namespace geode::stage;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

struct Seen {
    std::vector<std::string> log;
    std::vector<const void*> rings;
    uint32_t nextEvents = 0;
    std::string error;
    int moments = 0;
    int liquid = 0;
    bool stillReads = true;
    int stillRequests = 0;
    bool teeOpens = true;
    int starts = 0, fails = 0, stops = 0;
    bool freed = false;
    std::function<void()> during;  // runs inside a frame, as another thread's call would
};

class Viz final : public LiveViz {
public:
    explicit Viz(Seen& seen) : s_(seen) {}
    ~Viz() override { s_.freed = true; }
    void surfaceCreated() override { s_.log.push_back("created"); }
    void surfaceChanged(int width, int height) override { s_.log.push_back("size " + std::to_string(width) + "x" + std::to_string(height)); }
    uint32_t frame(const void* ring, float, float) override {
        s_.rings.push_back(ring);
        if (s_.during) s_.during();
        return s_.nextEvents;
    }
    void trimSpares() override { s_.log.push_back("trim"); }
    void letSoundGo() override { s_.log.push_back("let sound go"); }
    void releaseScenes() override { s_.log.push_back("release scenes"); }
    void warmTransition(const std::string& id) override { s_.log.push_back("warm " + id); }
    bool recordStart(const RecordAsk&) override {
        s_.starts++;
        return s_.teeOpens;
    }
    void recordFailed(const RecordAsk&) override { s_.fails++; }
    void recordStop(const RecordAsk&) override { s_.stops++; }
    std::string error() override { return s_.error; }
    int takeDirectorMoments() override {
        const int n = s_.moments;
        s_.moments = 0;
        return n;
    }
    int liquidMode() override { return s_.liquid; }
    bool readStill(Still& out) override {
        if (!s_.stillReads) return false;
        out.width = 2;
        out.height = 1;
        out.rgba.assign(8, 0x7f);
        return true;
    }
    void requestStill() override { s_.stillRequests++; }

private:
    Seen& s_;
};

struct Rig {
    Seen seen;
    LiveClient client{std::make_unique<Viz>(seen)};
};

void soundAndSurface() {
    Rig r;
    int ringA = 0, ringB = 0;
    r.client.surfaceCreated();
    r.client.surfaceChanged(1080, 2400);
    r.client.frame(60.0f, 60.0f);
    r.client.setSound(&ringA);
    r.client.frame(60.0f, 60.0f);
    r.client.setSound(&ringB);
    r.client.frame(60.0f, 60.0f);
    r.client.setSound(nullptr);
    r.client.frame(60.0f, 60.0f);
    check(r.seen.rings == std::vector<const void*>({nullptr, &ringA, &ringB, nullptr}), "each frame hears the ring set last");
    r.client.trimSpares();
    // Leaving drawing also lets the ring's analysis go, so it stops while nothing draws from it.
    check(r.seen.log == std::vector<std::string>({"created", "size 1080x2400", "trim", "let sound go"}),
          "the surface calls reach the renderer in order, and a stage that stops drawing stops listening");
    r.client.setSound(&ringA);
    r.client.frame(60.0f, 60.0f);
    check(r.seen.rings.back() == &ringA, "the next frame hears its ring again");
}

void soundSetDuringFrame() {
    Rig r;
    int ringA = 0, ringB = 0;
    r.client.setSound(&ringA);
    bool releasedInFrame = true;
    r.seen.during = [&] {
        r.client.setSound(&ringB);  // would wait for this very frame if the frame held the sound lock
        releasedInFrame = r.client.soundReleased(&ringA);
    };
    r.client.frame(60.0f, 60.0f);
    r.seen.during = nullptr;
    check(r.seen.rings == std::vector<const void*>({&ringA}), "a frame keeps the ring it began with");
    check(!releasedInFrame, "the ring in use is not released while its frame runs");
    check(r.client.soundReleased(&ringA), "it is released once that frame ends");
    check(!r.client.soundReleased(&ringB), "the ring set during the frame is the one now held");
    r.client.frame(60.0f, 60.0f);
    check(r.seen.rings.back() == &ringB, "the next frame hears the ring set during the last");
}

void frameReports() {
    Rig r;
    r.seen.nextEvents = kFrameClearOnly | kFrameErrorChanged;
    r.seen.error = "broken";
    check(r.client.frame(0.0f, 0.0f) == (kFrameClearOnly | kFrameErrorChanged) && r.client.error().empty(),
          "a cleared first frame reports nothing else");

    r.seen.nextEvents = kFrameErrorChanged;
    r.seen.error = "";
    check(r.client.frame(0.0f, 0.0f) == 0, "a clean renderer's first frame is no error news");
    r.seen.error = "bad shader";
    check(r.client.frame(0.0f, 0.0f) == kFrameErrorChanged && r.client.error() == "bad shader", "a new error text is");
    check(r.client.frame(0.0f, 0.0f) == 0, "the same text again is not");

    r.seen.nextEvents = kFrameDirectorMoment | kFrameLiquidChanged;
    r.seen.moments = 2;
    r.seen.liquid = 3;
    check(r.client.frame(0.0f, 0.0f) == r.seen.nextEvents, "director moments and the liquid mode are passed on");
    r.seen.moments = 1;
    r.client.frame(0.0f, 0.0f);
    check(r.client.takeDirectorMoments() == 3 && r.client.takeDirectorMoments() == 0, "moments are summed until taken");
    check(r.client.liquidMode() == 3, "the liquid mode is the last frame's");
    r.seen.nextEvents = kFrameDirectorMoment;
    check(r.client.frame(0.0f, 0.0f) == 0, "a bit with nothing behind it is dropped");

    Still still;
    r.seen.nextEvents = kFrameStillReady;
    r.seen.stillReads = false;
    check(r.client.frame(0.0f, 0.0f) == 0 && r.seen.stillRequests == 1 && !r.client.takeStill(still),
          "a still that could not be read is asked for again");
    r.seen.stillReads = true;
    check(r.client.frame(0.0f, 0.0f) == kFrameStillReady, "a still read is reported");
    check(r.client.takeStill(still) && still.width == 2 && still.height == 1 && still.rgba.size() == 8, "and handed over");
    check(!r.client.takeStill(still), "once");
    r.client.frame(0.0f, 0.0f);
    r.client.release(true);
    check(!r.client.takeStill(still), "a still never taken is dropped when the client releases");
}

void warmUps() {
    Rig r;
    check(r.client.warm("zoom"), "a warm-up is queued");
    r.client.service(false);
    check(r.seen.log.empty(), "and waits for a context");
    r.client.warm("spin");
    r.client.service(true);
    check(r.seen.log == std::vector<std::string>({"warm zoom", "warm spin"}), "then runs in the order asked");
    r.client.warm("fold");
    r.client.frame(0.0f, 0.0f);
    check(r.seen.log.back() == "warm fold", "a frame runs what waits before it draws");
}

void recordTee() {
    Rig r;
    check(r.client.recordClosed(), "no recording, nothing open");
    check(r.client.recordStart(RecordAsk{}) && !r.client.recordClosed(), "a tee asked for is open until its stop ran");
    r.client.service(true);
    check(r.seen.starts == 1 && r.seen.fails == 0, "it opens on the stage thread");
    r.client.recordStop();
    check(!r.client.recordClosed(), "and stays open until the stage thread closed it");
    r.client.service(false);
    check(r.seen.stops == 1 && r.client.recordClosed(), "a stop needs no context");
    r.client.recordStop();
    r.client.service(true);
    check(r.seen.stops == 1, "a second stop closes nothing");

    r.client.recordStart(RecordAsk{});
    r.client.service(false);
    check(r.seen.starts == 1 && r.seen.fails == 1 && !r.client.recordClosed(), "with no context the recording fails, and still waits for its stop");
    r.client.recordStop();
    r.client.service(false);
    check(r.seen.stops == 2 && r.client.recordClosed(), "which closes it");

    r.seen.teeOpens = false;
    r.client.recordStart(RecordAsk{});
    r.client.recordStop();
    r.client.service(true);
    check(r.seen.starts == 2 && r.seen.fails == 2 && r.seen.stops == 3 && r.client.recordClosed(), "a tee the driver refused fails and closes");
}

// Red against: a tee and its mark kept open across a lost context. Their GL names are gone, and the context made
// next gives the same numbers to the renderer's objects, which the mark then draws with and deletes at its stop.
void aLostContextEndsTheRecording() {
    Rig r;
    r.client.recordStart(RecordAsk{});
    r.client.service(true);
    r.client.contextLost();
    check(r.seen.fails == 1 && r.seen.stops == 1 && r.client.recordClosed(), "a lost context fails the recording and closes its tee");
    r.client.recordStop();
    r.client.service(true);
    check(r.seen.stops == 1, "the stop that follows closes nothing more");
    r.client.contextLost();
    check(r.seen.fails == 1 && r.seen.stops == 1, "with no recording a lost context does nothing");
}

void releases() {
    Rig r;
    r.seen.teeOpens = true;
    r.client.recordStart(RecordAsk{});
    r.client.service(true);
    r.client.recordStart(RecordAsk{});
    check(r.client.lockViz(), "a live renderer is handed to another thread");
    r.client.unlockViz();
    r.client.release(true);
    check(r.seen.starts == 1 && r.seen.fails == 1 && r.seen.stops == 2 && r.client.recordClosed(),
          "a release closes the open tee and fails the one still waiting");
    check(r.seen.log.back() == "release scenes" && r.seen.freed, "frees the scenes with the context current, then the renderer");
    check(!r.client.lockViz(), "a released renderer is handed to nobody");
    check(!r.client.warm("zoom") && !r.client.recordStart(RecordAsk{}) && !r.client.recordStop(), "and nothing is queued after it");
    check(r.client.recordClosed(), "so a recording refused there has nothing open");
    check(r.client.frame(60.0f, 60.0f) == 0, "a frame after it draws nothing");

    Rig bare;
    bare.client.release(false);
    check(bare.seen.log.empty() && bare.seen.freed, "without a context the scenes are left to the context's end");
}

}  // namespace

int main() {
    soundAndSurface();
    soundSetDuringFrame();
    frameReports();
    warmUps();
    recordTee();
    aLostContextEndsTheRecording();
    releases();
    if (failures) {
        std::fprintf(stderr, "%d live client check(s) failed\n", failures);
        return 1;
    }
    std::printf("live client: all checks passed\n");
    return 0;
}
