// Host-side checks for an export's render order (core/output/ExportFrame) against a recording renderer: the
// prepare order up to the look (offscreen pin, frame rate, surface, scene, MilkDrop preset, settings pin and its
// hand mask; a scene it cannot draw stops before the pin), the pre-roll (output/PreRoll's plan: the decode from
// fromMs heard for heardMs, then its end, then the range's decode opened from the range start; a track that does
// not open or is not heard fails), and each frame (the audio up to FrameClock::samplesThrough, then a draw at
// floor(n * 1e9 / fps) ns, still drawn once the track has run out). Built and run by CI
// (.github/workflows/native-host-tests.yml); no device.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/export_frame_test.cpp
//     core/output/ExportFrame.cpp core/output/FrameClock.cpp core/audio/ingest/TrackPump.cpp
//     core/audio/ingest/TrackFeed.cpp core/audio/ingest/AiffReader.cpp -o export_frame_test && ./export_frame_test
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio/ingest/AiffReader.hpp"
#include "audio/ingest/TrackPump.hpp"
#include "output/ExportFrame.hpp"
#include "output/FrameClock.hpp"
#include "output/PreRoll.hpp"

using geode::audio::ingest::AiffReader;
using geode::audio::ingest::AiffSource;
using geode::audio::ingest::MemorySource;
using geode::audio::ingest::TrackFeed;
using geode::audio::ingest::TrackSource;
using geode::output::ExportFrames;
using geode::output::ExportStart;
using geode::output::ExportTarget;
using geode::output::FrameClock;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

constexpr uint32_t kRate = 8000;

// ── A mono 16-bit AIFF ramp: frame f holds f % 30000 ─────────────────────────────────────────────────────────

void be16(std::vector<uint8_t>& b, int v) {
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void be32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

std::vector<uint8_t> ramp(uint32_t frames) {
    std::vector<uint8_t> body;
    tag(body, "AIFF");
    tag(body, "COMM");
    be32(body, 18);
    be16(body, 1);
    be32(body, frames);
    be16(body, 16);
    int exponent = 16383 + 31;
    uint32_t m = kRate;
    while (!(m & 0x80000000u)) {
        m <<= 1;
        --exponent;
    }
    be16(body, exponent);
    be32(body, m);
    be32(body, 0);
    tag(body, "SSND");
    be32(body, 8 + frames * 2);
    be32(body, 0);
    be32(body, 0);
    for (uint32_t f = 0; f < frames; ++f) be16(body, static_cast<int>(f % 30000));
    std::vector<uint8_t> file;
    tag(file, "FORM");
    be32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

float rampAt(int64_t frame) { return static_cast<float>(static_cast<int16_t>(frame % 30000)) / 32768.0f; }

// ── A renderer that writes down what it is asked ──────────────────────────────────────────────────────────────

class Recorder final : public ExportTarget {
public:
    bool hear(TrackFeed& feed) override {
        const TrackFeed::Sink sink = [](void* context, const float* interleaved, int frames, int, int) {
            auto* self = static_cast<Recorder*>(context);
            self->heard.insert(self->heard.end(), interleaved, interleaved + frames);
        };
        return feed.deliver(sink, this);
    }
    void prepareOffscreen() override { log.push_back("offscreen"); }
    void setFrameRate(int fps) override { log.push_back("fps " + std::to_string(fps)); }
    void surface(int width, int height) override {
        log.push_back("surface " + std::to_string(width) + "x" + std::to_string(height));
    }
    bool setScene(const std::string& sceneId) override {
        log.push_back("scene " + sceneId);
        return drawable;
    }
    void loadMilkPreset(const std::string& path) override { log.push_back("milk " + path); }
    uint32_t pinEngineSettings(bool entitled, int64_t durationMs) override {
        log.push_back("pin " + std::to_string(entitled ? 1 : 0) + " " + std::to_string(durationMs));
        return 0x5u;
    }
    bool endPreRoll(int64_t rangeStartMs) override {
        log.push_back("end " + std::to_string(rangeStartMs) + " after " + std::to_string(heard.size()));
        heardBeforeEnd = heard.size();
        return heardSomething;
    }
    void render(double timeSeconds, uint32_t targetFbo) override {
        log.push_back("render " + std::to_string(targetFbo) + " after " + std::to_string(heard.size()));
        times.push_back(timeSeconds);
    }

    std::vector<std::string> log;
    std::vector<float> heard;
    std::vector<double> times;
    size_t heardBeforeEnd = 0;
    bool drawable = true;
    bool heardSomething = true;
};

// Opens the ramp as the track, writing down each start it is opened from.
struct Opener {
    std::vector<uint8_t> bytes;
    std::vector<int64_t> starts;
    bool opens = true;

    geode::output::TrackOpener fn() {
        return [this](int64_t startMs) -> std::unique_ptr<TrackSource> {
            starts.push_back(startMs);
            if (!opens) return nullptr;
            return std::make_unique<AiffSource>(AiffReader::open(std::make_unique<MemorySource>(bytes)));
        };
    }
};

void beginOrder() {
    Opener track{ramp(kRate), {}, true};
    ExportFrames frames(30, 0, track.fn());
    Recorder r;
    ExportStart start;
    start.width = 1280;
    start.height = 720;
    start.sceneId = "neon";
    start.milkPresetPath = "/presets/a.milk";
    start.entitled = true;
    start.pinDurationMs = 77000;
    check(frames.begin(start, r) == 5, "begin returns the pinned hand mask");
    const std::vector<std::string> order = {"offscreen", "fps 30", "surface 1280x720", "scene neon",
                                            "milk /presets/a.milk", "pin 1 77000"};
    check(r.log == order, "the prepare order up to the look");
    check(track.starts.empty(), "begin opens no track");

    Recorder noMilk;
    start.milkPresetPath.clear();
    start.entitled = false;
    frames.begin(start, noMilk);
    check(noMilk.log.size() == 5 && noMilk.log[4] == "pin 0 77000", "no preset: none loaded");

    Recorder cannot;
    cannot.drawable = false;
    check(frames.begin(start, cannot) == -1, "a scene the renderer cannot draw fails begin");
    check(cannot.log.back() == "scene neon", "nothing is pinned after a scene it cannot draw");
}

// The pre-roll from range start rangeMs, then frames 0..count-1 at fps; checks against PreRoll and FrameClock.
void preRollAndFrames(int64_t rangeMs, int fps, int count) {
    const std::string at = " (range " + std::to_string(rangeMs) + " ms, " + std::to_string(fps) + " fps)";
    const geode::output::PreRoll plan = geode::output::preRollFor(rangeMs);
    Opener track{ramp(static_cast<uint32_t>(kRate * (rangeMs / 1000 + 30))), {}, true};  // 30 s past the range start
    ExportFrames frames(fps, rangeMs, track.fn());
    Recorder r;
    check(frames.preRoll(r), "the pre-roll is heard" + at);
    check(track.starts == std::vector<int64_t>({plan.fromMs, rangeMs}), "the pre-roll's decode, then the range's" + at);
    const auto heardFrames = static_cast<size_t>(plan.heardMs * kRate / 1000);
    check(r.heardBeforeEnd == heardFrames, "the engine hears heardMs before the pre-roll ends" + at);
    check(!r.heard.empty() && r.heard.front() == rampAt(plan.fromMs * kRate / 1000), "the pre-roll opens at fromMs" + at);
    check(r.log.size() == 1 && r.log[0] == "end " + std::to_string(rangeMs) + " after " + std::to_string(heardFrames),
          "the pre-roll ends at the range start" + at);

    const FrameClock clock(fps, static_cast<int>(kRate));
    bool frameExact = true;
    bool timeExact = true;
    for (int n = 0; n < count; ++n) {
        frames.frame(n, 7, r);
        const auto through = static_cast<size_t>(clock.samplesThrough(n));
        frameExact = frameExact && r.heard.size() == heardFrames + through;
        timeExact = timeExact && r.times.back() == static_cast<double>(clock.ptsNs(n)) / 1'000'000'000.0;
    }
    check(frameExact, "each frame hears exactly the audio to its end" + at);
    check(timeExact, "each frame draws at its exact time" + at);
    check(r.heard.size() > heardFrames && r.heard[heardFrames] == rampAt(rangeMs * kRate / 1000),
          "frame 0's audio starts at the range start" + at);
    check(r.log.back() == "render 7 after " + std::to_string(r.heard.size()), "the draw follows the frame's audio" + at);
}

void failures_() {
    Opener none{ramp(kRate), {}, false};
    ExportFrames a(30, 20000, none.fn());
    Recorder ra;
    check(!a.preRoll(ra) && ra.log.empty(), "a track that does not open fails before the pre-roll ends");

    Opener track{ramp(kRate * 30), {}, true};
    ExportFrames b(24, 20000, track.fn());
    Recorder rb;
    rb.heardSomething = false;
    check(!b.preRoll(rb), "an engine that heard nothing fails the render");
    check(track.starts.size() == 1, "and the range's decode is never opened");

    // A track shorter than the range: frames past its end are drawn all the same.
    Opener shortTrack{ramp(kRate * 3), {}, true};
    ExportFrames c(30, 0, shortTrack.fn());
    Recorder rc;
    check(c.preRoll(rc), "a three-second track is heard from its opening");
    check(c.frame(0, 0, rc), "frame 0 is heard");
    check(!c.frame(93, 0, rc) && rc.times.size() == 2, "a frame past the track's end is drawn, and says so");
}

}  // namespace

int main() {
    beginOrder();
    preRollAndFrames(20000, 30, 77);   // 8 s of track before the range: heard, played on into it
    preRollAndFrames(1000, 24, 93);    // under 2 s before it: the opening is heard, then the song starts over
    preRollAndFrames(0, 60, 141);      // a range at the track's start
    preRollAndFrames(174000, 30, 30);  // a later range
    failures_();
    if (failures) {
        std::printf("%d export frame check(s) failed\n", failures);
        return 1;
    }
    std::printf("export frame: all checks passed\n");
    return 0;
}
