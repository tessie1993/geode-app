// Host test for the video export job (core/output/VideoExport): the bit rate cap, the encoder it settles on, the
// frame count and presentation times, the audio interleaved by timestamp, the progress, what a cancel and each
// failure leave behind, and the order the render is torn down in. The picture and the surface are a fake Host;
// the encoder and the muxer are fakes of the encode sink's seam, fed by what the fake surface presents.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "output/EncodePlan.hpp"
#include "output/VideoExport.hpp"

namespace {

namespace ve = geode::output::videoexport;
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

struct Write {
    int track = -1;
    int64_t ptsUs = 0;
    int32_t size = 0;
};

struct World {
    // Script.
    int refuseAboveFps = 0;
    bool refuseAll = false;
    bool noMuxer = false;
    bool noAudioFormat = false;
    bool neverAnnounce = false;
    bool neverEnd = false;
    bool emptyBuffer = false;
    int32_t nullForSize = -1;  // a sample of this size comes with no buffer
    int failFrame = -1;
    int cancelFrame = -1;
    ve::Job* job = nullptr;

    // Record.
    std::deque<Output> ready;
    bool announced = false;
    std::vector<sink::VideoFormat> configured;
    std::vector<std::string> mimes;
    std::vector<int> trackFormats;  // 0 video, 1 audio, in addTrack order
    std::vector<Write> writes;
    std::vector<int64_t> presentedNs;
    std::vector<int64_t> presentedMs;
    std::vector<size_t> audioBeforePresent;
    std::vector<std::string> events;
    ve::Scene scene;
    int64_t markLengthMs = -1;
    int muxStarts = 0;
    int muxStops = 0;
    int idleWaits = 0;
    int reads = 0;
    int samplesOut = 0;
    int samplesReleased = 0;
    size_t audioCount = 0;

    size_t audioWrites() const { return audioCount; }
};

class FakeFormat final : public sink::TrackFormat {
public:
    explicit FakeFormat(int kind) : kind(kind) {}
    const int kind;
};

class FakeEncoder final : public sink::Encoder {
public:
    FakeEncoder(World& world, std::string mime) : w_(world), mime_(std::move(mime)) {}
    ~FakeEncoder() override { w_.events.push_back("encoder-gone"); }

    bool configure(const sink::VideoFormat& f) override {
        if (w_.refuseAll) return false;
        if (w_.refuseAboveFps > 0 && f.frameRate > w_.refuseAboveFps) return false;
        w_.configured.push_back(f);
        w_.mimes.push_back(mime_);
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
        if (out.kind == sink::Out::Sample) ++w_.samplesOut;
        size_ = out.info.size;
        return out.kind;
    }
    const uint8_t* outputBuffer(int, size_t* capacity) override {
        *capacity = sizeof(bytes_);
        return w_.emptyBuffer || size_ == w_.nullForSize ? nullptr : bytes_;
    }
    bool releaseOutput(int) override {
        ++w_.samplesReleased;
        return true;
    }
    std::unique_ptr<sink::TrackFormat> outputFormat() override { return std::make_unique<FakeFormat>(0); }

private:
    World& w_;
    std::string mime_;
    uint8_t bytes_[16] = {};
    int32_t size_ = 0;  // the size of the sample last dequeued
};

class FakeMuxer final : public sink::Muxer {
public:
    explicit FakeMuxer(World& world) : w_(world) {}
    ~FakeMuxer() override { w_.events.push_back("muxer-gone"); }
    int addTrack(const sink::TrackFormat& format) override {
        w_.trackFormats.push_back(static_cast<const FakeFormat&>(format).kind);
        return static_cast<int>(w_.trackFormats.size()) - 1;
    }
    bool start() override {
        ++w_.muxStarts;
        return true;
    }
    bool writeSample(int track, const uint8_t*, const sink::BufferInfo& info) override {
        w_.writes.push_back({track, info.ptsUs, info.size});
        if (track == 1) ++w_.audioCount;
        return true;
    }
    bool stop() override {
        ++w_.muxStops;
        return true;
    }

private:
    World& w_;
};

class FakeCodecs final : public sink::Codecs {
public:
    explicit FakeCodecs(World& world) : w_(world) {}
    std::unique_ptr<sink::Encoder> createEncoder(const char* mime) override {
        return std::make_unique<FakeEncoder>(w_, mime);
    }
    std::unique_ptr<sink::Muxer> createMuxer() override {
        if (w_.noMuxer) return nullptr;
        return std::make_unique<FakeMuxer>(w_);
    }
    std::unique_ptr<sink::TrackFormat> trackFormat(const std::vector<sink::FormatEntry>& entries) override {
        if (w_.noAudioFormat || entries.empty()) return nullptr;
        return std::make_unique<FakeFormat>(1);
    }

private:
    World& w_;
};

class FakeHost final : public ve::Host {
public:
    explicit FakeHost(World& world) : w_(world) {}

    std::unique_ptr<sink::Codecs> codecs() override { return std::make_unique<FakeCodecs>(w_); }
    int readAudio(const ve::AudioSample& sample, uint8_t* out) override {
        ++w_.reads;
        for (int i = 0; i < sample.size; ++i) out[i] = static_cast<uint8_t>(i);
        return sample.size;
    }
    void idleWait() override { ++w_.idleWaits; }
    const char* openSurface(void* window) override {
        w_.events.push_back(window ? "open-surface" : "open-surface-null");
        return nullptr;
    }
    const char* prepareScene(const ve::Scene& scene) override {
        w_.scene = scene;
        w_.events.push_back("prepare-scene");
        return nullptr;
    }
    const char* prepareMark(int64_t lengthMs) override {
        w_.markLengthMs = lengthMs;
        return nullptr;
    }
    bool renderFrame(int64_t frame) override {
        if (frame == w_.cancelFrame && w_.job) w_.job->cancel();
        return frame != w_.failFrame;
    }
    const char* present(int64_t ptsNs, int64_t timeMs) override {
        w_.audioBeforePresent.push_back(w_.audioWrites());
        w_.presentedNs.push_back(ptsNs);
        w_.presentedMs.push_back(timeMs);
        if (!w_.neverAnnounce && !w_.announced) {
            w_.ready.push_back({sink::Out::FormatChanged, {}});
            w_.announced = true;
        }
        // The codec-config buffer first, then the frame, stamped as the surface was.
        if (w_.presentedNs.size() == 1) w_.ready.push_back({sink::Out::Sample, {8, 0, sink::kFlagCodecConfig}});
        w_.ready.push_back({sink::Out::Sample, {16, ptsNs / 1000, 0}});
        return nullptr;
    }
    void closeScene() override { w_.events.push_back("close-scene"); }
    void closeSurface() override { w_.events.push_back("close-surface"); }

private:
    World& w_;
};

// AAC at 48 kHz: one 1024-sample frame every 21 333 us.
constexpr int kRate = 48000;
constexpr int64_t kAacFrameUs = 1024LL * 1'000'000 / kRate;

ve::Request request(int64_t seconds, int fps = 30) {
    ve::Request r;
    r.width = 1920;
    r.height = 1080;
    r.bitRate = 12'000'000;
    r.fps = fps;
    r.mimes[0] = "video/hevc";
    r.mimes[1] = "video/avc";
    r.codecCount = 2;
    const int64_t count = seconds * 1'000'000 / kAacFrameUs;
    for (int64_t i = 0; i < count; ++i) {
        r.audio.push_back({i * 300, 300, i * kAacFrameUs, i == 0 ? 1u : 0u});
    }
    sink::FormatEntry mime;
    mime.key = "mime";
    mime.type = sink::kFormatString;
    mime.text = "audio/mp4a-latm";
    sink::FormatEntry rate;
    rate.key = "sample-rate";
    rate.type = sink::kFormatInt32;
    rate.whole = kRate;
    r.audioFormat = {mime, rate};
    r.audioBytes = count * 300;
    return r;
}

struct Run {
    World world;
    FakeHost host{world};
    std::unique_ptr<ve::Job> job;

    explicit Run(const ve::Request& r) {
        job = std::make_unique<ve::Job>(r, host);
        world.job = job.get();
    }
    void go() { job->run(); }
};

void testProgressAndDuration() {
    check(ve::leadInProgress(0.0f) == 0.0f, "the lead-in starts at 0");
    check(std::fabs(ve::leadInProgress(0.5f) - 0.05f) < 1e-6f, "half the lead-in is 0.05");
    check(ve::leadInProgress(2.0f) == ve::kLeadInSpan, "the lead-in never passes its span");
    check(ve::frameProgress(0, 100) == ve::kLeadInSpan, "the first frame starts after the lead-in");
    check(std::fabs(ve::frameProgress(50, 100) - (0.1f + 0.425f)) < 1e-6f, "half the frames");
    check(ve::frameProgress(3, 0) == ve::kLeadInSpan, "no frames keeps the lead-in");

    const ve::Request r = request(2);
    const int64_t end = ve::audioDurationUs(r.audio, r.audioFormat);
    check(end == r.audio.back().ptsUs + kAacFrameUs, "the audio ends one AAC frame past its last sample");
    check(ve::audioDurationUs({}, r.audioFormat) == 0, "no audio runs 0");
}

void testFullRender() {
    const ve::Request r = request(2, 30);
    Run run(r);
    check(run.job->progress() == ve::kLeadInSpan, "a job starts where the lead-in ended");
    run.go();
    const World& w = run.world;
    check(run.job->state() == ve::State::Rendered, "rendered");
    check(run.job->failure() == ve::Failure::None, "no failure");
    check(run.job->progress() == 1.0f, "progress ends at 1");
    check(run.job->fps() == 30 && run.job->bitRate() == 12'000'000, "the first rung on the first codec");
    check(w.mimes.size() == 1 && w.mimes[0] == "video/hevc", "the requested codec first");
    check(w.configured[0].iFrameIntervalSec == 1 && w.configured[0].width == 1920, "the format asked for");

    const int64_t durationUs = ve::audioDurationUs(r.audio, r.audioFormat);
    const int64_t frames = durationUs * 30 / 1'000'000;
    check(run.job->frames() == frames, "frames = floor(duration * fps)");
    check(static_cast<int64_t>(w.presentedNs.size()) == frames, "every frame presented");
    bool exact = true;
    for (size_t i = 0; i < w.presentedNs.size(); ++i) {
        exact = exact && w.presentedNs[i] == static_cast<int64_t>(i) * 1'000'000'000 / 30;
        exact = exact && w.presentedMs[i] == static_cast<int64_t>(i) * 1000 / 30;
    }
    check(exact, "each frame's timestamp and time from its index");
    check(w.scene.fps == 30 && w.scene.durationMs == durationUs / 1000, "the scene pinned for the audio's length");
    check(w.markLengthMs == durationUs / 1000, "the mark knows the length");

    check(w.trackFormats.size() == 2 && w.trackFormats[0] == 0 && w.trackFormats[1] == 1, "video then audio");
    check(w.muxStarts == 1 && w.muxStops == 1, "the muxer started and finished once");
    check(w.audioWrites() == r.audio.size(), "every audio sample written");
    int64_t last = -1;
    bool sorted = true;
    size_t video = 0;
    for (const Write& wr : w.writes) {
        if (wr.track == 1) {
            sorted = sorted && wr.ptsUs > last;
            last = wr.ptsUs;
        } else {
            ++video;
        }
    }
    check(sorted, "audio in timestamp order");
    check(static_cast<int64_t>(video) == frames, "one video sample per frame, no codec config");
    // Before frame k is presented, the audio written is every sample before frame k-1's time.
    bool interleaved = true;
    for (size_t k = 1; k < w.audioBeforePresent.size(); ++k) {
        const int64_t upTo = w.presentedMs[k - 1] * 1000;
        size_t due = 0;
        for (const ve::AudioSample& s : r.audio) due += s.ptsUs < upTo ? 1 : 0;
        interleaved = interleaved && w.audioBeforePresent[k] == due;
    }
    check(interleaved, "audio interleaved up to each frame's time");

    const std::vector<std::string> tail(w.events.end() - 4, w.events.end());
    check(tail == std::vector<std::string>({"close-scene", "muxer-gone", "encoder-gone", "close-surface"}),
          "teardown: scene, muxer, encoder, surface");
}

void testBitRateCap() {
    // An hour at 40 Mbit/s would pass the muxer's limit.
    ve::Request r = request(1);
    r.bitRate = 40'000'000;
    const int64_t hourUs = 3600LL * 1'000'000;
    r.audio.push_back({0, 300, hourUs, 0});
    Run run(r);
    run.go();
    const int64_t durationUs = hourUs + kAacFrameUs;
    const int expected = geode::output::encode::cappedVideoBps(40'000'000, durationUs, r.audioBytes);
    check(expected < 40'000'000, "the cap lowers an hour at 4K's rate");
    check(run.world.configured[0].bitRate == expected, "the encoder asked for the capped rate");
}

void testLadder() {
    ve::Request r = request(1, 60);
    Run run(r);
    run.world.refuseAboveFps = 30;
    run.go();
    check(run.job->state() == ve::State::Rendered, "the fallback rung renders");
    check(run.job->fps() == 30 && run.job->bitRate() == 8'000'000, "half the rate at 2/3 of the bit rate");
    check(run.world.presentedNs.size() > 1 && run.world.presentedNs[1] == 1'000'000'000 / 30,
          "frames on the rung's grid");
}

void testRejected() {
    Run run(request(1));
    run.world.refuseAll = true;
    run.go();
    check(run.job->state() == ve::State::Failed, "rejected fails");
    check(run.job->failure() == ve::Failure::EncoderRejected, "rejected says so");
    check(run.world.scene.fps == 0 && run.world.presentedNs.empty(), "nothing prepared or drawn");
    check(run.world.events.back() == "close-surface", "torn down all the same");
}

void testCancel() {
    Run run(request(2));
    run.world.cancelFrame = 5;
    run.go();
    check(run.job->state() == ve::State::Cancelled, "cancelled");
    check(run.world.presentedNs.size() == 6, "the frame being drawn finishes, no more");
    check(run.world.muxStops == 1, "a muxer holding samples is stopped so it can be released");
    check(run.world.idleWaits == 0, "a cancel does not wait for the encoder's tail");

    Run late(request(1));
    late.go();
    late.job->cancel();
    check(late.job->state() == ve::State::Rendered, "a cancel after the finish leaves the job's outcome to the shell");
}

void testFailures() {
    {
        Run run(request(1));
        run.world.neverAnnounce = true;
        run.go();
        check(run.job->failure() == ve::Failure::NoOutput, "no format is no output");
        check(run.world.muxStops == 0, "a muxer that never started is not stopped");
    }
    {
        Run run(request(1));
        run.world.neverEnd = true;
        run.go();
        check(run.job->failure() == ve::Failure::Stalled, "no end of stream stalls");
        check(run.world.idleWaits == ve::kMaxIdlePolls, "ten seconds of polls");
    }
    {
        Run run(request(1));
        run.world.failFrame = 3;
        run.go();
        check(run.job->failure() == ve::Failure::Stopped, "a frame that cannot hear stops");
        check(run.job->detail() == "the track's audio could not be decoded", "and says why");
    }
    {
        Run run(request(1));
        run.world.noMuxer = true;
        run.go();
        check(run.job->failure() == ve::Failure::Stopped && run.world.configured.empty(), "no file, no encoder");
    }
    {
        Run run(request(1));
        run.world.noAudioFormat = true;
        run.go();
        check(run.job->failure() == ve::Failure::Stopped, "an audio track with no format stops");
    }
    {
        Run run(request(1));
        run.world.emptyBuffer = true;
        run.go();
        check(run.job->failure() == ve::Failure::EmptyBuffer, "a sample with no buffer");
    }
    {
        // A frame's 16 bytes with no buffer: bytes the codec cannot hand over are a broken codec.
        Run run(request(1));
        run.world.nullForSize = 16;
        run.go();
        check(run.job->failure() == ve::Failure::EmptyBuffer, "a frame's bytes with no buffer fail");
        check(run.world.samplesReleased == run.world.samplesOut, "the bufferless frame is handed back");
    }
}

void testEmptyEndOfStream() {
    // The end-of-stream marker holds nothing, and a codec may hand it with no buffer: the export finishes.
    const ve::Request r = request(1);
    Run run(r);
    run.world.nullForSize = 0;
    run.go();
    const World& w = run.world;
    check(run.job->failure() == ve::Failure::None, "an empty end-of-stream sample with no buffer is no failure");
    check(run.job->state() == ve::State::Rendered, "the export with a bufferless end of stream renders");
    check(w.muxStops == 1 && w.audioWrites() == r.audio.size(), "its end of stream is honoured: audio fed, file finished");
    check(w.idleWaits == 0, "no wait for an end of stream already seen");
    check(w.samplesOut > 0 && w.samplesReleased == w.samplesOut, "every sample handed back, the bufferless one too");
}

}  // namespace

int main() {
    testProgressAndDuration();
    testFullRender();
    testBitRateCap();
    testLadder();
    testRejected();
    testCancel();
    testFailures();
    testEmptyEndOfStream();
    if (failures == 0) std::printf("video_export_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
