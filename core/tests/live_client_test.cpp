// Host-side checks for core/stage/LiveClient: what a live stage's client does between the stage thread and the
// threads that talk to it (docs/agents/native-stage.md). The renderer is a model that records every call.
//
// What is pinned: a look's parts reach the renderer only when they changed (all of them the first time); a frame
// hears the ring set last and none after it is cleared; what a frame reported is kept until polled (an error text
// only when it differs, each loaded preset once and in order, director moments summed, the liquid mode, a still,
// asked for again when it could not be read); a transition's warm-up waits for a context; a tee opens on the
// stage thread, fails without a context, and closes exactly once, on its stop or on the stage's release; nothing
// is queued after the release, and a renderer released is no longer handed to other threads.
#include <cstdint>
#include <cstdio>
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
    std::vector<std::string> loaded;
    int moments = 0;
    int liquid = 0;
    bool stillReads = true;
    int stillRequests = 0;
    bool teeOpens = true;
    int starts = 0, fails = 0, stops = 0;
    bool freed = false;
};

class Viz final : public LiveViz {
public:
    explicit Viz(Seen& seen) : s_(seen) {}
    ~Viz() override { s_.freed = true; }
    void surfaceCreated() override { s_.log.push_back("created"); }
    void surfaceChanged(int width, int height) override { s_.log.push_back("size " + std::to_string(width) + "x" + std::to_string(height)); }
    uint32_t frame(const void* ring, float, float) override {
        s_.rings.push_back(ring);
        return s_.nextEvents;
    }
    void trimSpares() override { s_.log.push_back("trim"); }
    void releaseScenes() override { s_.log.push_back("release scenes"); }
    void warmTransition(const std::string& id) override { s_.log.push_back("warm " + id); }
    bool recordStart(const RecordAsk&) override {
        s_.starts++;
        return s_.teeOpens;
    }
    void recordFailed(const RecordAsk&) override { s_.fails++; }
    void recordStop(const RecordAsk&) override { s_.stops++; }
    std::string error() override { return s_.error; }
    std::string takeMilkLoaded() override {
        if (s_.loaded.empty()) return {};
        std::string path = s_.loaded.front();
        s_.loaded.erase(s_.loaded.begin());
        return path;
    }
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

Look lookAt(float tempo) {
    Look l;
    l.sceneId = "tunnel";
    l.params = {tempo, 0.5f};
    l.lfos = "wave " + std::to_string(tempo);
    l.adsrs = "envelope a";
    l.tuning = {0.5f};
    l.transitionId = "fade";
    l.transitionMs = 1200;
    return l;
}

void lookParts() {
    LookLatch latch;
    check(latch.take(lookAt(77.0f)) == kLookAll, "the first look is every part");
    check(latch.take(lookAt(77.0f)) == 0, "the same look again is nothing");
    check(latch.take(lookAt(93.0f)) == (kLookParams | kLookLfos), "only the parts that differ");
    Look next = lookAt(93.0f);
    next.sceneId = "bloom";
    next.transitionMs = 400;
    check(latch.take(next) == (kLookScene | kLookTransition), "a new scene and a new transition length");
    next.hasReactions = true;
    check(latch.take(next) == kLookReactions, "an empty user text is not the style's own");
    next.reactions = "route a";
    next.reducedMotion = true;
    next.tuning = {0.75f};
    next.adsrs = "envelope b";
    check(latch.take(next) == (kLookReactions | kLookReducedMotion | kLookTuning | kLookAdsrs), "routes, reduced motion, tuning and envelopes");
    check(latch.look().sceneId == "bloom" && latch.look().transitionMs == 400, "and it keeps the look last taken");
}

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
    check(r.seen.log == std::vector<std::string>({"created", "size 1080x2400", "trim"}), "the surface calls reach the renderer in order");
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

    r.seen.nextEvents = kFrameMilkLoaded | kFrameDirectorMoment | kFrameLiquidChanged;
    r.seen.loaded = {"a.milk"};
    r.seen.moments = 2;
    r.seen.liquid = 3;
    check(r.client.frame(0.0f, 0.0f) == r.seen.nextEvents, "a loaded preset, director moments and the liquid mode are passed on");
    r.seen.loaded = {"b.milk"};
    r.seen.moments = 1;
    r.client.frame(0.0f, 0.0f);
    check(r.client.takeMilkLoaded() == "a.milk" && r.client.takeMilkLoaded() == "b.milk" && r.client.takeMilkLoaded().empty(),
          "each loaded preset once, in order");
    check(r.client.takeDirectorMoments() == 3 && r.client.takeDirectorMoments() == 0, "moments are summed until taken");
    check(r.client.liquidMode() == 3, "the liquid mode is the last frame's");
    r.seen.nextEvents = kFrameMilkLoaded | kFrameDirectorMoment;
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
    lookParts();
    soundAndSurface();
    frameReports();
    warmUps();
    recordTee();
    releases();
    if (failures) {
        std::fprintf(stderr, "%d live client check(s) failed\n", failures);
        return 1;
    }
    std::printf("live client: all checks passed\n");
    return 0;
}
