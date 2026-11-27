// Host test for cast/CastEncoder and its PcmBacklog, with fake codecs behind output/EncodeSink's seam: the
// encoders configured from the cast format, the video one on a surface; the PCM backlog's order, drops and PTS;
// codec config buffers into the fMP4 init segment and to the WebRTC tap; frames to both; the Opus encoder made
// only while a browser listens, fed the same PCM, its header not sent, freed after ten idle seconds and never
// tried again once refused; a video or AAC codec error ends the stream, an Opus one only the browser's sound; the
// drain thread names its own nice.
//
// Build & run:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore -Icore/tests/stub core/tests/cast_encoder_test.cpp
//       core/cast/CastEncoder.cpp core/cast/Fmp4Writer.cpp -o cast_encoder_test && ./cast_encoder_test
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cast/CastEncoder.hpp"
#include "util/ThreadNice.hpp"

namespace sink = geode::output::sink;
using geode::cast::CastEncoder;
using geode::cast::PcmBacklog;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

using Buf = std::vector<uint8_t>;

struct Output {
    Buf bytes;
    int64_t ptsUs = 0;
    uint32_t flags = 0;
    bool error = false;
};

// One fake codec: what it was configured with, the PCM it was given, and the outputs the test scripts.
struct FakeState {
    std::string mime;
    sink::VideoFormat video;
    sink::AudioFormat audio;
    bool surface = false;
    bool started = false;
    bool refuseStart = false;
    bool released = false;
    std::vector<std::pair<int64_t, size_t>> inputs;   // (pts, bytes)
    std::deque<Output> outputs;
    Buf input = Buf(4096 * 4);
    Buf held;
    bool inputError = false;
    std::atomic<int> drainNice{geode::util::kNiceInherited};   // the nice of the thread that last drained it
};

class FakeEncoder final : public sink::Encoder {
public:
    explicit FakeEncoder(std::shared_ptr<FakeState> s) : s_(std::move(s)) {}
    ~FakeEncoder() override { s_->released = true; }
    bool configure(const sink::VideoFormat& f) override {
        s_->video = f;
        return true;
    }
    bool configure(const sink::AudioFormat& f) override {
        s_->audio = f;
        return true;
    }
    bool useSurfaceInput() override {
        s_->surface = true;
        return true;
    }
    void* inputWindow() const override { return s_->surface ? s_.get() : nullptr; }
    bool start() override {
        s_->started = !s_->refuseStart;
        return s_->started;
    }
    void stop() override { s_->started = false; }
    bool endSurfaceInput() override { return true; }
    int dequeueInput() override { return s_->inputError ? kInputError : 0; }
    uint8_t* inputBuffer(int, size_t* capacity) override {
        *capacity = s_->input.size();
        return s_->input.data();
    }
    bool queueInput(int, size_t bytes, int64_t ptsUs, uint32_t) override {
        s_->inputs.emplace_back(ptsUs, bytes);
        return true;
    }
    sink::Out dequeueOutput(int* index, sink::BufferInfo* info) override {
        s_->drainNice = geode::util::threadNice();
        if (s_->outputs.empty()) return sink::Out::None;
        if (s_->outputs.front().error) return sink::Out::Error;
        s_->held = s_->outputs.front().bytes;
        *index = 0;
        *info = {static_cast<int32_t>(s_->held.size()), s_->outputs.front().ptsUs, s_->outputs.front().flags};
        s_->outputs.pop_front();
        return sink::Out::Sample;
    }
    const uint8_t* outputBuffer(int, size_t* capacity) override {
        *capacity = s_->held.size();
        return s_->held.data();
    }
    bool releaseOutput(int) override { return true; }
    std::unique_ptr<sink::TrackFormat> outputFormat() override { return nullptr; }

private:
    std::shared_ptr<FakeState> s_;
};

struct FakeCodecs final : public sink::Codecs {
    std::vector<std::shared_ptr<FakeState>>* made;
    bool refuseOpus = false;
    explicit FakeCodecs(std::vector<std::shared_ptr<FakeState>>* m) : made(m) {}
    std::unique_ptr<sink::Encoder> createEncoder(const char* mime) override {
        auto s = std::make_shared<FakeState>();
        s->mime = mime;
        s->refuseStart = refuseOpus && s->mime == CastEncoder::kMimeOpus;
        made->push_back(s);
        return std::make_unique<FakeEncoder>(s);
    }
    std::unique_ptr<sink::Muxer> createMuxer() override { return nullptr; }
};

const Buf kConfig = {0, 0, 0, 1, 0x67, 0x42, 0xC0, 0x1E, 0xDA, 0, 0, 0, 1, 0x68, 0xCE, 0x3C, 0x80};

CastEncoder::Format format() {
    CastEncoder::Format f;
    f.width = 1280;
    f.height = 720;
    f.fps = 30;
    f.keyframeSec = 1;
    f.videoBitRate = 4'000'000;
    f.audioBitRate = 160'000;
    f.rateHz = 48'000;
    f.channels = 2;
    return f;
}

struct Seen {
    std::vector<Buf> stream;
    std::vector<Buf> configs;
    int videoFrames = 0;
    std::vector<Buf> opus;
    int peers = 0;
};

CastEncoder::Outputs outputs(Seen& seen) {
    CastEncoder::Outputs o;
    o.stream = [&seen](const uint8_t* d, size_t n) { seen.stream.emplace_back(d, d + n); };
    o.videoConfig = [&seen](const uint8_t* d, size_t n) { seen.configs.emplace_back(d, d + n); };
    o.video = [&seen](const uint8_t*, size_t, int64_t, bool) { ++seen.videoFrames; };
    o.opus = [&seen](const uint8_t* d, size_t n, int64_t) { seen.opus.emplace_back(d, d + n); };
    o.peers = [&seen] { return seen.peers; };
    return o;
}

void testBacklog() {
    PcmBacklog b(4, 2);   // a one-second cap of 4 frames
    const int16_t block[] = {1, 1, 2, 2, 3, 3};
    b.push(block, 3);
    check(b.pending() == 0, "inactive: nothing kept");
    b.start();
    b.push(block, 3);
    check(b.pending() == 3, "three frames wait");
    const int16_t more[] = {4, 4, 5, 5};
    b.push(more, 2);   // over the cap by one: the oldest goes
    check(b.pending() == 4, "the cap holds");
    int16_t out[8] = {};
    int64_t pts = -1;
    check(b.take(out, 2, &pts) == 2 && out[0] == 2 && out[2] == 3, "the oldest left first");
    check(pts == 250'000, "the dropped frame moved the clock on: frame 1 of 4 a second");
    check(b.take(out, 8, &pts) == 2 && out[0] == 4 && out[2] == 5 && pts == 750'000, "the rest, stamped on");
    check(b.take(out, 8, &pts) == 0, "none wait");
    b.stop();
    b.push(block, 3);
    check(b.pending() == 0, "stopped: nothing kept");
}

void testStream() {
    std::vector<std::shared_ptr<FakeState>> made;
    Seen seen;
    auto e = CastEncoder::open(std::make_unique<FakeCodecs>(&made), format(), outputs(seen));
    check(e != nullptr, "opened");
    if (!e) return;
    check(made.size() == 2 && made[0]->mime == "video/avc" && made[1]->mime == "audio/mp4a-latm", "H.264 and AAC");
    check(made[0]->surface && made[0]->started && made[1]->started, "both started, the video on a surface");
    check(made[0]->video.width == 1280 && made[0]->video.bitRate == 4'000'000 && made[0]->video.frameRate == 30 &&
              made[0]->video.iFrameIntervalSec == 1,
          "the video format is the cast's");
    check(made[1]->audio.sampleRateHz == 48'000 && made[1]->audio.channels == 2 && made[1]->audio.bitRate == 160'000,
          "the AAC format is the cast's");
    check(e->inputWindow() == made[0].get(), "the surface is the video encoder's");

    // A frame's PCM reaches the AAC input with its PTS.
    std::vector<int16_t> pcm(1600 * 2, 7);
    e->queuePcm(pcm.data(), 1600);
    e->step(0);
    check(made[1]->inputs.size() == 1 && made[1]->inputs[0].first == 0 && made[1]->inputs[0].second == 1600 * 4,
          "one AAC input of a frame's PCM at 0");
    e->queuePcm(pcm.data(), 1600);
    e->step(0);
    check(made[1]->inputs.size() == 2 && made[1]->inputs[1].first == 1600 * 1'000'000LL / 48'000, "the next at 1600 frames on");

    // Configs, then a keyframe and frames, then AAC.
    made[0]->outputs.push_back({kConfig, 0, sink::kFlagCodecConfig, false});
    made[1]->outputs.push_back({{0x11, 0x90}, 0, sink::kFlagCodecConfig, false});
    for (int i = 0; i < 40; ++i) {
        const Buf frame = {0, 0, 0, 1, static_cast<uint8_t>(i % 30 == 0 ? 0x65 : 0x41), static_cast<uint8_t>(i)};
        made[0]->outputs.push_back({frame, i * 33'333LL, i % 30 == 0 ? sink::kFlagKeyFrame : 0u, false});
    }
    for (int i = 0; i < 60; ++i) made[1]->outputs.push_back({{0x21, static_cast<uint8_t>(i)}, i * 21'333LL, 0, false});
    for (int i = 0; i < 20; ++i) e->step(0);
    check(seen.configs.size() == 1 && seen.configs[0] == kConfig, "the SPS and PPS to WebRTC as the codec gave them");
    check(seen.videoFrames == 40, "every frame to WebRTC");
    check(!seen.stream.empty() && seen.stream[0].size() > 8 && std::memcmp(seen.stream[0].data() + 4, "ftyp", 4) == 0,
          "the init segment first");
    check(seen.stream.size() == 2 && std::memcmp(seen.stream[1].data() + 4, "moof", 4) == 0, "one fragment cut at the second keyframe");
    check(e->writer().fragments() == 1, "the writer counts it");
    check(!e->opusRunning() && seen.opus.empty(), "no Opus without a browser");

    // A browser comes: Opus is made and fed the same PCM, from 0.
    seen.peers = 1;
    e->step(1'000);
    check(e->opusRunning() && made.size() == 3 && made[2]->mime == "audio/opus" && made[2]->started, "Opus made for a browser");
    check(made[2]->audio.sampleRateHz == 48'000 && made[2]->audio.bitRate == 160'000, "at the cast's rate");
    e->queuePcm(pcm.data(), 1600);
    e->step(2'000);
    check(made[2]->inputs.size() == 1 && made[2]->inputs[0].first == 0, "Opus gets the PCM, its PTS from 0");
    made[2]->outputs.push_back({{0x4F, 0x70}, 0, sink::kFlagCodecConfig, false});
    made[2]->outputs.push_back({{0xFC, 1}, 0, 0, false});
    e->step(3'000);
    check(seen.opus.size() == 1 && seen.opus[0] == Buf({0xFC, 1}), "the Opus header is not sent, a packet is");

    // The browser leaves: Opus waits ten seconds, then goes.
    seen.peers = 0;
    e->step(4'000);
    check(e->opusRunning(), "Opus kept while a browser may come back");
    e->step(4'000 + CastEncoder::kOpusIdleNs);
    check(e->opusRunning(), "not before ten seconds");
    e->step(4'001 + CastEncoder::kOpusIdleNs);
    check(!e->opusRunning() && made[2]->released, "freed after ten idle seconds");
    e->queuePcm(pcm.data(), 1600);   // no Opus: kept for the AAC alone

    // An Opus error loses only the browser's sound, for good.
    seen.peers = 1;
    e->step(20'000'000'000);
    check(e->opusRunning() && made.size() == 4, "a new Opus for a new browser");
    made[3]->outputs.push_back({{}, 0, 0, true});
    e->step(20'000'000'001);
    check(!e->opusRunning() && !e->failed(), "an Opus error ends Opus, not the stream");
    e->step(20'000'000'002);
    check(made.size() == 4, "and it is not tried again");

    // A video error ends the stream.
    made[0]->outputs.push_back({{}, 0, 0, true});
    check(!e->step(30'000'000'000) && e->failed(), "a video error fails the stream");
    check(!e->step(30'000'000'001), "nothing moves after");
    e.reset();
    check(made[0]->released && made[1]->released, "every codec released");
}

void testRefused() {
    std::vector<std::shared_ptr<FakeState>> made;
    Seen seen;
    auto codecs = std::make_unique<FakeCodecs>(&made);
    codecs->refuseOpus = true;
    auto e = CastEncoder::open(std::move(codecs), format(), outputs(seen));
    check(e != nullptr, "opened");
    if (!e) return;
    seen.peers = 2;
    e->step(0);
    check(!e->opusRunning() && made.size() == 3 && made[2]->released, "a refused Opus is released");
    e->step(1);
    check(made.size() == 3, "and not tried again");
    CastEncoder::Format bad = format();
    bad.rateHz = 0;
    check(CastEncoder::open(std::make_unique<FakeCodecs>(&made), bad, outputs(seen)) == nullptr, "no rate, no encoder");
}

void testAacInputError() {
    std::vector<std::shared_ptr<FakeState>> made;
    Seen seen;
    auto e = CastEncoder::open(std::make_unique<FakeCodecs>(&made), format(), outputs(seen));
    if (!e) return;
    made[1]->inputError = true;
    std::vector<int16_t> pcm(100 * 2, 1);
    e->queuePcm(pcm.data(), 100);
    check(!e->step(0) && e->failed(), "an AAC input error fails the stream");
}

void testThread() {
    std::vector<std::shared_ptr<FakeState>> made;
    Seen seen;
    auto e = CastEncoder::open(std::make_unique<FakeCodecs>(&made), format(), outputs(seen));
    if (!e) return;
    e->start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (made[0]->drainNice == geode::util::kNiceInherited && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    // A cast's encoder is opened on the cast render thread (nice -4): its drain must not inherit that.
    check(made[0]->drainNice == geode::util::kFeedNice, "the drain thread sets its own nice, not its spawner's");
    e.reset();   // joins
    check(made[0]->released, "the thread stops and the codecs go");
}

}  // namespace

int main() {
    testBacklog();
    testStream();
    testRefused();
    testAacInputError();
    testThread();
    if (failures == 0) std::printf("cast_encoder_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
