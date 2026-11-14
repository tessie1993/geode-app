// Host-side checks for core/viz/frame/LiveFrame: one live frame as one call (geode_viz_frame). Built and run by
// CI ("Native host tests" in .github/workflows/native-host-tests.yml) with the system compiler; no GL.
//
// What is pinned: the stages run in one order (paced rate, quality, Liquid label, gyro, scene, ring, render, then
// the events); a new surface's first frame clears and runs nothing else; a scene the renderer refuses is asked
// again every frame until it takes it, and then not again; the gyro resets once per session (each switch on, and
// once when it goes off), never once a frame; quality is applied once per change and again on a new surface; and each event bit is set exactly when it is due and
// matches the ABI.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/frame/LiveFrame.hpp"

using namespace geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static_assert(kEventClearOnly == GEODE_FRAME_CLEAR_ONLY);
static_assert(kEventLiquidChanged == GEODE_FRAME_LIQUID_CHANGED);
static_assert(kEventErrorChanged == GEODE_FRAME_ERROR_CHANGED);
static_assert(kEventMilkLoaded == GEODE_FRAME_MILK_LOADED);
static_assert(kEventDirectorMoment == GEODE_FRAME_DIRECTOR_MOMENT);
static_assert(kEventStillReady == GEODE_FRAME_STILL_READY);

// Records every stage it is asked for, by name, and answers what the test sets.
struct Recorder : LiveStages {
    std::vector<std::string> log;
    int liquid = -1;
    std::string knownScene = "a";
    std::string error;
    bool milk = false;
    bool moments = false;
    bool still = false;
    std::string scene;
    UserQuality quality;
    GyroReading gyro;
    float paced = 0.0f;
    float cap = 0.0f;

    void setPacedFps(float pacedFps, float capFps) override {
        log.push_back("paced");
        paced = pacedFps;
        cap = capFps;
    }
    void applyQuality(const UserQuality& q) override {
        log.push_back("quality");
        quality = q;
    }
    void clearToBlack() override { log.push_back("clear"); }
    int takeLiquidMode() override {
        log.push_back("liquid");
        return liquid;
    }
    void gyroReset() override { log.push_back("gyroReset"); }
    void gyroSample(const GyroReading& r) override {
        log.push_back("gyro");
        gyro = r;
    }
    bool setScene(const std::string& id) override {
        log.push_back("scene");
        if (id != knownScene) return false;
        scene = id;
        return true;
    }
    void feedRing() override { log.push_back("ring"); }
    void render() override { log.push_back("render"); }
    std::string lastError() override {
        log.push_back("error");
        return error;
    }
    bool milkPresetLoaded() override {
        log.push_back("milk");
        return milk;
    }
    bool directorMomentsPending() override {
        log.push_back("director");
        return moments;
    }
    bool stillReady() override {
        log.push_back("still");
        return still;
    }

    int count(const char* name) const {
        int n = 0;
        for (const auto& s : log) n += s == name ? 1 : 0;
        return n;
    }
};

// A frame on a surface already past its clear, with the log reset first.
uint32_t frame(LiveFrame& f, Recorder& r, float paced = 60.0f, float cap = 60.0f) {
    r.log.clear();
    return f.run(r, paced, cap);
}

void stageOrder() {
    LiveFrame f;
    Recorder r;
    f.surfaceCreated();
    f.setQuality(UserQuality{0.75f, 1.0f, true});
    f.requestScene("a");
    f.gyroPush(GyroReading{0.1f, 0.2f, 0.3f, 0.9f, 1});
    frame(f, r);
    r.liquid = 1;
    frame(f, r, 90.0f, 120.0f);
    // The quality went on the clear frame; this one has nothing new for it.
    const std::vector<std::string> want = {"paced", "liquid", "gyroReset", "gyro", "scene", "ring",
                                           "render", "error", "milk", "director", "still"};
    check(r.log == want, "the stages run in their one order");
    check(r.paced == 90.0f && r.cap == 120.0f, "the paced rate and the cap reach the renderer apart");
}

void clearOnlyRunsNothingElse() {
    LiveFrame f;
    Recorder r;
    f.requestScene("a");
    f.gyroPush(GyroReading{});
    f.surfaceCreated();
    r.liquid = 1;
    r.milk = r.moments = r.still = true;
    r.error = "boom";
    const uint32_t events = frame(f, r);
    check(events == kEventClearOnly, "a new surface's first frame says clear-only and nothing else");
    check(r.log == std::vector<std::string>{"paced", "clear"}, "and runs nothing but the clear");
    const uint32_t next = frame(f, r);
    check((next & kEventClearOnly) == 0, "only the first frame is a clear");
    check(r.count("render") == 1 && r.count("scene") == 1 && r.count("gyro") == 1, "the next frame draws");
    check(frame(f, r) != kEventClearOnly && r.count("clear") == 0, "and so does every frame after");
    f.surfaceCreated();
    check(frame(f, r) == kEventClearOnly, "every new surface clears its first frame");
}

void qualityOncePerChange() {
    LiveFrame f;
    Recorder r;
    frame(f, r);
    check(r.count("quality") == 0, "nothing set: the renderer keeps its own");
    f.setQuality(UserQuality{0.5f, 0.5f, false});
    frame(f, r);
    check(r.count("quality") == 1 && r.quality.renderScale == 0.5f && !r.quality.postFx, "a set is applied");
    frame(f, r);
    check(r.count("quality") == 0, "once");
    f.setQuality(UserQuality{0.5f, 0.5f, false});
    frame(f, r);
    check(r.count("quality") == 0, "an equal set is not applied again");
    f.setQuality(UserQuality{1.0f, 0.5f, false});
    frame(f, r);
    check(r.count("quality") == 1 && r.quality.renderScale == 1.0f, "a change is");
    f.surfaceCreated();
    frame(f, r);
    check(r.count("quality") == 1 && r.count("clear") == 1, "a new surface applies it again, on its clear frame");
}

void sceneRetriedUntilAccepted() {
    LiveFrame f;
    Recorder r;
    frame(f, r);
    check(r.count("scene") == 0, "no request: the scene is not touched");
    r.knownScene = "later";
    f.requestScene("later");
    r.knownScene = "";  // not drawable yet
    frame(f, r);
    frame(f, r);
    check(r.count("scene") == 1 && r.scene.empty(), "a refused scene is asked for again on the next frame");
    r.knownScene = "later";
    frame(f, r);
    check(r.count("scene") == 1 && r.scene == "later", "until the renderer takes it");
    frame(f, r);
    check(r.count("scene") == 0, "and then not again");
    f.requestScene("later");
    frame(f, r);
    check(r.count("scene") == 1, "a new request, even of the same id, is applied once");
    r.knownScene = "b";
    f.requestScene("x");
    f.requestScene("b");
    frame(f, r);
    check(r.count("scene") == 1 && r.scene == "b", "only the latest of two requests is applied");
}

void gyroResetsOncePerSession() {
    LiveFrame f;
    Recorder r;
    frame(f, r);
    check(r.count("gyroReset") == 0 && r.count("gyro") == 0, "off from the start: no reset, no sample");
    f.gyroPush(GyroReading{0.0f, 0.0f, 0.0f, 1.0f, 0});
    frame(f, r);
    check(r.count("gyroReset") == 1 && r.count("gyro") == 1, "the first reading of a session resets, then samples");
    f.gyroPush(GyroReading{0.5f, 0.0f, 0.0f, 0.8f, 3});
    frame(f, r);
    check(r.count("gyroReset") == 0 && r.count("gyro") == 1 && r.gyro.x == 0.5f && r.gyro.rotation == 3,
          "later readings only sample, the newest one");
    frame(f, r);
    check(r.count("gyroReset") == 0 && r.count("gyro") == 1, "with no new reading the last one is sampled again");
    f.gyroOff();
    frame(f, r);
    check(r.count("gyroReset") == 1 && r.count("gyro") == 0, "off recentres once");
    frame(f, r);
    check(r.count("gyroReset") == 0, "not once a frame");
    f.gyroPush(GyroReading{});
    f.gyroOff();
    f.gyroPush(GyroReading{});
    frame(f, r);
    check(r.count("gyroReset") == 1 && r.count("gyro") == 1, "off and on again between frames is one new session");
    f.gyroOff();
    f.gyroPush(GyroReading{});
    frame(f, r);
    check(r.count("gyroReset") == 1, "a switch on no frame saw off still recaptures");
}

void eventBits() {
    LiveFrame f;
    Recorder r;
    f.surfaceCreated();
    frame(f, r);
    check(frame(f, r) == kEventErrorChanged, "a handle's first drawing frame reports its error text, even a clean one");
    check(frame(f, r) == 0, "a quiet frame says nothing");
    r.liquid = 1;
    check(frame(f, r) == kEventLiquidChanged && f.liquidMode() == 1, "a new Liquid mode is told, and readable");
    r.liquid = -1;
    check(frame(f, r) == 0 && f.liquidMode() == 1, "an unchanged one is not, and the mode stays");
    r.liquid = 0;
    check(frame(f, r) == kEventLiquidChanged && f.liquidMode() == 0, "off is a change too");
    r.liquid = -1;
    r.error = "shader failed";
    check(frame(f, r) == kEventErrorChanged, "a new error is told");
    check(frame(f, r) == 0, "once");
    r.milk = true;
    check(frame(f, r) == kEventMilkLoaded, "a loaded preset is told");
    r.milk = false;
    r.moments = true;
    check(frame(f, r) == kEventDirectorMoment, "pending director moments are told");
    r.moments = false;
    r.still = true;
    check(frame(f, r) == kEventStillReady, "a still read back is told");
    r.still = false;
    const uint32_t all[] = {kEventClearOnly, kEventLiquidChanged, kEventErrorChanged, kEventMilkLoaded, kEventDirectorMoment,
                            kEventStillReady};
    uint32_t seen = 0;
    bool apart = true;
    for (uint32_t bit : all) {
        apart = apart && (seen & bit) == 0 && bit != 0 && (bit & (bit - 1)) == 0;
        seen |= bit;
    }
    check(apart, "every event is a bit of its own");
}

}  // namespace

int main() {
    stageOrder();
    clearOnlyRunsNothingElse();
    qualityOncePerChange();
    sceneRetriedUntilAccepted();
    gyroResetsOncePerSession();
    eventBits();
    if (failures) {
        std::fprintf(stderr, "live_frame_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("live_frame_test: ok\n");
    return 0;
}
