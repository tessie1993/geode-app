// Host test for the loop render job (core/output/LoopRender): the encoder it settles on, the frame counts and
// presentation times of a stop, the seam the reel reports, the drift each stop adds, what a cancel and each
// failure leave behind, and the order a stop is torn down in. The picture, the surface and the files are a fake
// Host; the encoder and the muxer are fakes of the encode sink's seam, fed by what the fake surface presents.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "output/EncodePlan.hpp"
#include "output/LoopRender.hpp"

namespace {

namespace lr = geode::output::looprender;
namespace sink = geode::output::sink;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Output {
    sink::Out kind = sink::Out::None;
    sink::BufferInfo info{};
};

struct FileRecord {
    std::string path;
    std::vector<int64_t> ptsUs;
    int muxStarts = 0;
    int muxStops = 0;
    bool removed = false;
    bool closed = false;
};

// What the fakes share: the encoder's pending output, what was asked of each part, and the script.
struct World {
    // Script.
    std::string refusedMime;      // an encoder for this mime refuses to configure
    int refuseAboveFps = 0;       // > 0: configure refuses a higher frame rate
    bool refuseAll = false;
    bool noMuxer = false;
    bool neverAnnounce = false;   // the encoder never reports its format
    bool neverEnd = false;        // the encoder never reaches its end of stream
    int errorAfterPresents = -1;  // the encoder fails once this many frames were presented
    int failFileAt = -1;          // createFile fails for this stop
    std::vector<int> seamCaps;    // per stop, the most seam frames the stash holds (missing: all asked for)
    std::string seamError;        // the seam pass does not build
    int failFrame = -1;           // renderFrame fails at this frame
    int throwFrame = -1;          // renderFrame throws at this frame
    int failPresentAt = -1;       // the swap fails once this many frames were presented
    int cancelStop = -1;          // cancel while this stop draws ...
    int cancelFrame = -1;         // ... this frame
    lr::Job* job = nullptr;

    // Record.
    std::deque<Output> ready;
    bool announced = false;
    std::vector<sink::VideoFormat> configured;
    std::vector<std::string> configuredMimes;
    std::vector<FileRecord> files;
    std::vector<std::string> events;
    std::vector<lr::StopScene> scenes;
    std::vector<int> seamRequests;
    std::vector<int64_t> presented;  // this stop's presentation times, ns
    int presentsTotal = 0;
    int framesThisStop = 0;
    int captures = 0;
    int lastCapture = -1;
    int blends = 0;
    bool blendsInOrder = true;
    int idleWaits = 0;
    int stop = -1;
};

class FakeFormat final : public sink::TrackFormat {};

class FakeEncoder final : public sink::Encoder {
public:
    FakeEncoder(World& world, std::string mime) : w_(world), mime_(std::move(mime)) {}
    ~FakeEncoder() override { w_.events.push_back("encoder-gone"); }

    bool configure(const sink::VideoFormat& f) override {
        if (w_.refuseAll || mime_ == w_.refusedMime) return false;
        if (w_.refuseAboveFps > 0 && f.frameRate > w_.refuseAboveFps) return false;
        w_.configured.push_back(f);
        w_.configuredMimes.push_back(mime_);
        return true;
    }
    bool configure(const sink::AudioFormat&) override { return false; }
    bool useSurfaceInput() override { return true; }
    void* inputWindow() const override { return &w_; }
    bool start() override { return true; }
    void stop() override {}
    bool endSurfaceInput() override {
        if (!w_.neverEnd) w_.ready.push_back({sink::Out::Sample, {0, 0, sink::kFlagEndOfStream}});
        return true;
    }
    int dequeueInput() override { return kNoInput; }
    uint8_t* inputBuffer(int, size_t*) override { return nullptr; }
    bool queueInput(int, size_t, int64_t, uint32_t) override { return false; }
    sink::Out dequeueOutput(int* index, sink::BufferInfo* info) override {
        if (w_.ready.empty()) return sink::Out::None;
        const Output out = w_.ready.front();
        w_.ready.pop_front();
        *index = 0;
        *info = out.info;
        return out.kind;
    }
    const uint8_t* outputBuffer(int, size_t* capacity) override {
        *capacity = sizeof(bytes_);
        return bytes_;
    }
    bool releaseOutput(int) override { return true; }
    std::unique_ptr<sink::TrackFormat> outputFormat() override { return std::make_unique<FakeFormat>(); }

private:
    World& w_;
    std::string mime_;
    uint8_t bytes_[16] = {};
};

class FakeMuxer final : public sink::Muxer {
public:
    FakeMuxer(World& world, int file) : w_(world), file_(file) {}
    ~FakeMuxer() override { w_.events.push_back("muxer-gone"); }
    int addTrack(const sink::TrackFormat&) override { return 0; }
    bool start() override {
        ++w_.files[static_cast<size_t>(file_)].muxStarts;
        return true;
    }
    bool writeSample(int, const uint8_t*, const sink::BufferInfo& info) override {
        w_.files[static_cast<size_t>(file_)].ptsUs.push_back(info.ptsUs);
        return true;
    }
    bool stop() override {
        ++w_.files[static_cast<size_t>(file_)].muxStops;
        return true;
    }

private:
    World& w_;
    int file_;
};

class FakeCodecs final : public sink::Codecs {
public:
    FakeCodecs(World& world, int file) : w_(world), file_(file) {}
    std::unique_ptr<sink::Encoder> createEncoder(const char* mime) override {
        return std::make_unique<FakeEncoder>(w_, mime);
    }
    std::unique_ptr<sink::Muxer> createMuxer() override {
        if (file_ < 0 || w_.noMuxer) return nullptr;
        return std::make_unique<FakeMuxer>(w_, file_);
    }

private:
    World& w_;
    int file_;
};

class FakeHost final : public lr::Host {
public:
    explicit FakeHost(World& world) : w_(world) {}

    std::unique_ptr<sink::Codecs> codecs(int fd) override { return std::make_unique<FakeCodecs>(w_, fd); }
    int createFile(int stop, std::string* path) override {
        if (stop == w_.failFileAt) {
            *path = "no space left";
            return -1;
        }
        *path = "geode_loop_" + std::to_string(stop) + "_x.mp4";
        w_.files.push_back({*path, {}, 0, 0, false, false});
        return static_cast<int>(w_.files.size()) - 1;
    }
    void closeFile(int fd) override {
        w_.files[static_cast<size_t>(fd)].closed = true;
        w_.events.push_back("close-file");
    }
    void removeFile(const std::string& path) override {
        for (FileRecord& file : w_.files) {
            if (file.path == path) file.removed = true;
        }
    }
    void idleWait() override { ++w_.idleWaits; }

    const char* openSurface(void* window) override {
        ++w_.stop;
        w_.framesThisStop = 0;
        w_.presented.clear();
        w_.ready.clear();
        w_.announced = false;
        w_.events.push_back(window ? "open-surface" : "open-surface-null");
        return nullptr;
    }
    int allocateSeam(int requested, std::string* error) override {
        w_.events.push_back("seam");
        w_.seamRequests.push_back(requested);
        if (!w_.seamError.empty()) {
            *error = w_.seamError;
            return -1;
        }
        const size_t stop = static_cast<size_t>(w_.stop);
        return stop < w_.seamCaps.size() && w_.seamCaps[stop] < requested ? w_.seamCaps[stop] : requested;
    }
    const char* prepareScene(const lr::StopScene& scene) override {
        w_.events.push_back("scene");
        w_.scenes.push_back(scene);
        return nullptr;
    }
    const char* prepareMark() override {
        w_.events.push_back("mark");
        return nullptr;
    }
    bool renderFrame(int frame) override {
        if (frame == w_.failFrame) return false;
        if (frame == w_.throwFrame) throw std::runtime_error("out of memory");
        ++w_.framesThisStop;
        if (w_.job && w_.stop == w_.cancelStop && frame == w_.cancelFrame) w_.job->cancel();
        return true;
    }
    void captureSeam(int index) override {
        ++w_.captures;
        w_.lastCapture = index;
    }
    void blendSeam(int index, int tail, int seamFrames) override {
        if (index != w_.blends || tail != index || seamFrames != w_.seamRequests.back()) w_.blendsInOrder = false;
        ++w_.blends;
    }
    const char* present(int64_t ptsNs) override {
        if (static_cast<int>(w_.presented.size()) == w_.failPresentAt) return "the swap failed";
        w_.presented.push_back(ptsNs);
        ++w_.presentsTotal;
        if (!w_.announced && !w_.neverAnnounce) {
            w_.announced = true;
            w_.ready.push_back({sink::Out::FormatChanged, {}});
            w_.ready.push_back({sink::Out::Sample, {5, 0, sink::kFlagCodecConfig}});
        }
        if (static_cast<int>(w_.presented.size()) == w_.errorAfterPresents) {
            w_.ready.push_back({sink::Out::Error, {}});
            return nullptr;
        }
        w_.ready.push_back({sink::Out::Sample, {10, ptsNs / 1000, 0}});
        return nullptr;
    }
    void closeScene() override { w_.events.push_back("close-scene"); }
    void closeSurface() override { w_.events.push_back("close-surface"); }

private:
    World& w_;
};

lr::Request request(int width, int height, int bitRate, int fps) {
    lr::Request r;
    r.width = width;
    r.height = height;
    r.bitRate = bitRate;
    r.loopMs = 30'000;
    r.crossfadeMs = 1'000;
    r.fps = fps;
    r.mimes[0] = sink::kMimeAvc;
    r.codecCount = 1;
    return r;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

// One stop at 60 fps: 1800 loop frames and a 60-frame tail are drawn, the first 60 are stashed, 1800 are
// presented on the truncated 16 666 666 ns grid, and the stop is torn down scene first, surface last.
void oneStop() {
    World w;
    FakeHost host(w);
    lr::Job job(request(1280, 720, 6'000'000, 60), host);
    check(job.state() == lr::State::Idle, "a job starts idle");
    job.run();
    check(job.state() == lr::State::Rendered, "one stop renders");
    check(job.failure() == lr::Failure::None, "a rendered job has no failure");
    check(job.fps() == 60 && job.bitRate() == 6'000'000, "the request's rate and bit rate are kept");
    check(job.crossfadeMs() == 1'000, "60 seam frames at 60 fps report 1000 ms");
    check(job.progress() == 1.0f, "a rendered job reports 1");
    check(job.files().size() == 1 && job.files()[0] == "geode_loop_0_x.mp4", "one file, the stop's");
    check(w.framesThisStop == 1860, "1800 loop frames + 60 tail frames are drawn");
    check(w.captures == 60 && w.lastCapture == 59, "the first 60 frames are stashed");
    check(w.blends == 60 && w.blendsInOrder, "the 60 tail frames blend from the stash in order");
    check(w.presented.size() == 1800, "1800 frames are presented");
    check(w.presented.front() == 0, "the first presented frame is at 0");
    check(w.presented[1] == 16'666'666, "the second is one truncated frame later");
    check(w.presented.back() == 29'983'332'134LL, "the last is frame 1799 on that grid");
    check(w.scenes.size() == 1 && w.scenes[0].fps == 60 && w.scenes[0].pinDurationMs == 31'000,
          "the settings are pinned for 1860 frames at 60 fps: 31 000 ms");
    check(w.scenes[0].hueOffset == 0.0f && w.scenes[0].warmthOffset == 0.0f, "one stop does not drift");
    const FileRecord& file = w.files[0];
    check(file.ptsUs.size() == 1800, "every presented frame is written once; codec config and the end are not");
    check(file.ptsUs.front() == 0 && file.ptsUs.back() == 29'983'332, "samples keep the encoder's times");
    check(file.muxStarts == 1 && file.muxStops == 1, "the muxer starts once and stops once");
    check(file.closed && !file.removed, "the file is closed and kept");
    // Both configures (the probe's and the stop's) ask for the same format.
    check(w.configured.size() == 2, "the probe and the stop each configure an encoder");
    const sink::VideoFormat& f = w.configured.back();
    check(f.width == 1280 && f.height == 720 && f.bitRate == 6'000'000 && f.frameRate == 60, "the stop's format");
    check(f.iFrameIntervalSec == 1 && f.colorFormat == sink::kColorFormatSurface, "a keyframe a second, from a surface");
    check(w.idleWaits == 0, "an encoder that ends at once is never waited on");
    const std::vector<std::string> order = {"encoder-gone", "open-surface", "seam",         "scene",         "mark",
                                            "close-scene",  "muxer-gone",   "encoder-gone", "close-surface", "close-file"};
    check(w.events == order, "probe, surface, seam, scene, mark; then scene, muxer, encoder, surface, file");
}

// The requested codec refuses, and H.264 lists 30 fps as its top rate: the ladder's 60 fps rung is skipped
// and the job settles on 30 fps at two thirds of the bit rate, on H.264.
void ladderAndFallback() {
    World w;
    w.refusedMime = "video/hevc";
    FakeHost host(w);
    lr::Request r = request(1280, 720, 6'000'000, 60);
    r.mimes[0] = "video/hevc";
    r.mimes[1] = sink::kMimeAvc;
    r.topFps[1] = 30;
    r.codecCount = 2;
    lr::Job job(r, host);
    job.run();
    check(job.state() == lr::State::Rendered, "the fallback renders");
    check(job.fps() == 30 && job.bitRate() == 4'000'000, "30 fps at 2/3 of 6 Mbit/s");
    check(w.configuredMimes.back() == sink::kMimeAvc, "the stop encodes H.264");
    check(w.framesThisStop == 930, "900 loop frames + 30 tail frames at 30 fps");
    check(w.presented.size() == 900 && w.presented[1] == 33'333'333, "900 frames on the 33 333 333 ns grid");
    check(w.presented.back() == 29'966'666'367LL, "the last is frame 899 on that grid");
    check(w.scenes[0].pinDurationMs == 31'000, "930 frames at 30 fps pin 31 000 ms");
    check(job.crossfadeMs() == 1'000, "30 seam frames at 30 fps report 1000 ms");
}

void nothingAccepts() {
    World w;
    w.refuseAll = true;
    FakeHost host(w);
    lr::Job job(request(1280, 720, 6'000'000, 60), host);
    job.run();
    check(job.state() == lr::State::Failed && job.failure() == lr::Failure::EncoderRejected, "no encoder: rejected");
    check(w.files.empty() && job.files().empty(), "no file is made before an encoder is settled");
}

// At 3840x2160 a frame is 33 177 600 bytes, so the 192 MB stash holds 6: the seam is cut to 6 frames
// (100 ms at 60 fps) before the first stop, and the stop draws 1806 frames.
void seamBudget() {
    World w;
    FakeHost host(w);
    lr::Job job(request(3840, 2160, 40'000'000, 60), host);
    job.run();
    check(job.state() == lr::State::Rendered, "a 4K loop renders");
    check(w.seamRequests.size() == 1 && w.seamRequests[0] == 6, "the stash is asked for the 6 frames it can hold");
    check(w.framesThisStop == 1806 && w.presented.size() == 1800, "1806 drawn, 1800 presented");
    check(w.scenes[0].pinDurationMs == 30'100, "1806 frames at 60 fps pin 30 100 ms");
    check(job.crossfadeMs() == 100, "6 seam frames at 60 fps report 100 ms");
}

// A long form of three hours at 40 Mbit/s would pass the muxer's target: the rate is capped before the probe.
void longFormCap() {
    World w;
    FakeHost host(w);
    lr::Request r = request(1280, 720, 40'000'000, 60);
    r.longFormMs = 10'800'000;
    lr::Job job(r, host);
    job.run();
    const int64_t us = 10'800'000LL * 1000;
    const int capped = geode::output::encode::cappedVideoBps(40'000'000, us, geode::output::encode::audioBytes(us));
    check(capped < 40'000'000, "three hours at 40 Mbit/s is capped");
    check(job.state() == lr::State::Rendered && job.bitRate() == capped, "the stop encodes at the capped rate");
    check(w.configured.back().bitRate == capped, "the encoder is configured with it");
}

// Three stops drift from nothing to the full offset, and the second stop's stash holds only 40 frames: the
// third is asked for no more, and the reel reports the 40 (666 ms).
void driftAndShortestSeam() {
    World w;
    w.seamCaps = {60, 40};
    FakeHost host(w);
    lr::Request r = request(1280, 720, 6'000'000, 60);
    r.hueTurns = 0.3f;
    r.warmth = 0.6f;
    r.stops = 3;
    lr::Job job(r, host);
    job.run();
    check(job.state() == lr::State::Rendered, "three stops render");
    check(job.files().size() == 3 && job.files()[2] == "geode_loop_2_x.mp4", "one file per stop, in order");
    check(w.seamRequests == std::vector<int>({60, 60, 40}), "a later stop asks for no more than an earlier one kept");
    check(job.crossfadeMs() == 666, "40 seam frames at 60 fps report 666 ms");
    check(w.scenes.size() == 3, "three scenes");
    check(near(w.scenes[0].hueOffset, 0.0f) && near(w.scenes[0].warmthOffset, 0.0f), "the first stop is the look");
    check(near(w.scenes[1].hueOffset, 0.15f) && near(w.scenes[1].warmthOffset, 0.3f), "the middle stop is half way");
    check(near(w.scenes[2].hueOffset, 0.3f) && near(w.scenes[2].warmthOffset, 0.6f), "the last stop is the full drift");
    check(w.scenes[1].pinDurationMs == 30'666 && w.scenes[2].pinDurationMs == 30'666, "1840 frames pin 30 666 ms");
    check(w.presentsTotal == 5400, "each stop presents its 1800 frames");
    check(w.files[1].ptsUs.size() == 1800 && w.files[1].ptsUs.front() == 0, "each file starts at 0");
}

// A cancel seen before frame 101 of the second stop: nothing more is drawn, and both files go.
void cancelMidway() {
    World w;
    w.cancelStop = 1;
    w.cancelFrame = 100;
    FakeHost host(w);
    lr::Request r = request(1280, 720, 6'000'000, 60);
    r.hueTurns = 0.2f;
    r.stops = 2;
    lr::Job job(r, host);
    w.job = &job;
    job.run();
    check(job.state() == lr::State::Cancelled, "a cancel ends the job cancelled");
    check(w.framesThisStop == 101, "the frame being drawn finishes; no other starts");
    check(job.files().empty(), "a cancelled job hands out no file");
    check(w.files.size() == 2 && w.files[0].removed && w.files[1].removed, "the finished loop and the pending one go");
    check(w.files[1].muxStops == 1, "a muxer that took samples is stopped on the way out");
    check(job.progress() < 1.0f, "a cancelled job never reports 1");
}

// A throw out of a frame unwinds through the stop: its scene, muxer, encoder, surface and file still close, in
// the order a finished stop closes them, so nothing stays open for the caller to find.
void throwMidStop() {
    World w;
    w.throwFrame = 100;
    FakeHost host(w);
    lr::Job job(request(1280, 720, 6'000'000, 60), host);
    bool thrown = false;
    try {
        job.run();
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    check(thrown, "the throw reaches the caller");
    check(w.files.size() == 1 && w.files[0].closed, "the stop's file is closed");
    check(w.files[0].muxStops == 1, "a muxer that took samples is stopped");
    const std::vector<std::string> want{"close-scene", "muxer-gone", "encoder-gone", "close-surface", "close-file"};
    const bool ends = w.events.size() >= want.size() && std::equal(want.begin(), want.end(), w.events.end() - 5);
    check(ends, "scene, muxer, encoder, surface, file: the teardown's order");
}

lr::Job* runFailing(World& w, FakeHost& host, std::unique_ptr<lr::Job>& job) {
    job = std::make_unique<lr::Job>(request(1280, 720, 6'000'000, 60), host);
    job->run();
    check(job->state() == lr::State::Failed, "the job failed");
    check(job->files().empty(), "a failed job hands out no file");
    for (const FileRecord& file : w.files) check(file.removed && file.closed, "a failed job leaves no file behind");
    return job.get();
}

void failuresAreNamed() {
    std::unique_ptr<lr::Job> job;
    {
        World w;
        w.seamError = "0:12 syntax error";
        FakeHost host(w);
        const lr::Job* j = runFailing(w, host, job);
        check(j->failure() == lr::Failure::ShaderCompile && j->detail() == "0:12 syntax error", "the seam's log");
        check(w.events.back() == "close-file" && w.scenes.empty(), "no scene is made without a seam");
    }
    {
        World w;
        w.seamCaps = {0};
        FakeHost host(w);
        check(runFailing(w, host, job)->failure() == lr::Failure::LowGpuMemory, "no seam frame fits");
    }
    {
        World w;
        w.failFrame = 70;
        FakeHost host(w);
        const lr::Job* j = runFailing(w, host, job);
        check(j->failure() == lr::Failure::Stopped, "a frame that cannot be drawn stops the job");
        check(j->detail() == "the track's audio could not be decoded", "and says why");
        check(w.files[0].muxStops == 1, "the muxer had samples, so it is stopped");
    }
    {
        World w;
        w.failPresentAt = 3;
        FakeHost host(w);
        const lr::Job* j = runFailing(w, host, job);
        check(j->failure() == lr::Failure::Stopped && j->detail() == "the swap failed", "a frame the encoder did not get");
        check(w.presented.size() == 3, "no frame follows it");
    }
    {
        World w;
        w.neverAnnounce = true;
        FakeHost host(w);
        check(runFailing(w, host, job)->failure() == lr::Failure::NoOutput, "an encoder with no format: no output");
        check(w.files[0].ptsUs.empty() && w.files[0].muxStops == 0, "nothing was written and no muxer stopped");
    }
    {
        World w;
        w.neverEnd = true;
        FakeHost host(w);
        check(runFailing(w, host, job)->failure() == lr::Failure::Stalled, "an encoder with no end: stalled");
        check(w.idleWaits == lr::kMaxIdlePolls, "after 1000 waits of 10 ms");
        check(w.files[0].muxStops == 1, "the muxer is still stopped on the way out");
    }
    {
        World w;
        w.errorAfterPresents = 5;
        FakeHost host(w);
        check(runFailing(w, host, job)->failure() == lr::Failure::EncoderFailed, "an encoder error is the encoder's");
        check(w.presented.size() == 5, "nothing is presented after it");
    }
    {
        World w;
        w.failFileAt = 0;
        FakeHost host(w);
        const lr::Job* j = runFailing(w, host, job);
        check(j->failure() == lr::Failure::CacheWrite && j->detail() == "no space left", "the file's reason");
    }
    {
        World w;
        w.noMuxer = true;
        FakeHost host(w);
        check(runFailing(w, host, job)->failure() == lr::Failure::CacheWrite, "a file the muxer cannot open");
        check(w.stop == -1, "no surface is opened without a writer");
    }
}

void driftOnTheParamsFrame() {
    std::vector<float> params = {0.5f, 0.9f, 0.25f, 0.5f};
    lr::driftParams(params, 1, 3, 0.25f, 0.8f);
    check(near(params[1], 0.15f), "0.9 + 0.25 wraps into the turn at 0.15");
    check(near(params[3], 1.0f), "0.5 + 0.8 is held at 1");
    check(params[0] == 0.5f && params[2] == 0.25f, "no other slot moves");
    std::vector<float> same = params;
    lr::driftParams(same, -1, 9, 0.5f, 0.5f);
    check(same == params, "a slot outside the frame is left alone");
}

}  // namespace

int main() {
    oneStop();
    ladderAndFallback();
    nothingAccepts();
    seamBudget();
    longFormCap();
    driftAndShortestSeam();
    cancelMidway();
    throwMidStop();
    failuresAreNamed();
    driftOnTheParamsFrame();
    if (failures == 0) std::printf("loop_render_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
