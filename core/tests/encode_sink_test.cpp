// Host-side checks for the encode sink (core/output/EncodeSink) and Record's session on it
// (core/output/RecordSession), on a fake codec and a fake muxer: the encoder ladder falls through refused rungs
// and stops at the first that works; samples that come out before the muxer starts are held and written in
// order after; a format change mid-stream keeps the track; codec-config buffers stay out of the file; the end
// of stream drains everything; PTS never go backwards per track; a codec error stops with the failure; no
// sample is written twice or dropped; and the session's stop waits for the stage's tee, or a second.
// Built and run by CI (.github/workflows/native-host-tests.yml, "Encode sink host test"); no device.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "output/EncodeSink.hpp"
#include "output/RecordSession.hpp"
#include "output/Recorder.hpp"

using namespace geode::output;
using sink::BufferInfo;
using sink::EncodeSink;
using sink::Out;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// One output the fake encoder hands out on a dequeue.
struct Step {
    Out kind = Out::None;
    BufferInfo info;
    std::vector<uint8_t> bytes;  // the whole buffer; the sample is info.offset .. + info.size
};

Step formatChanged() { return {Out::FormatChanged, {}, {}}; }

// A sample whose bytes all equal tag, after `offset` bytes of padding the muxer must skip.
Step sample(int64_t ptsUs, uint8_t tag, int size = 8, uint32_t flags = 0, int offset = 0) {
    Step s;
    s.kind = Out::Sample;
    s.info = {offset, size, ptsUs, flags};
    s.bytes.assign(static_cast<size_t>(offset), 0xEE);
    s.bytes.insert(s.bytes.end(), static_cast<size_t>(size), tag);
    return s;
}

struct Queued {
    size_t bytes = 0;
    int64_t ptsUs = 0;
    uint32_t flags = 0;
    std::vector<int16_t> pcm;
};

// What one fake encoder saw and will do, shared with the test so it outlives the encoder.
struct EncState {
    std::string mime;
    bool refuseConfigure = false;
    bool refuseSurface = false;
    bool configured = false;
    sink::VideoFormat video;
    sink::AudioFormat audio;
    bool started = false;
    bool stopped = false;
    bool released = false;
    int endedInput = 0;
    bool eosOnEndInput = false;  // a surface's end puts out an empty end-of-stream buffer
    bool eosOnEosInput = false;  // an end-of-stream input does the same
    bool errorNext = false;
    size_t inputCapacity = 4096 * 4;
    bool unaligned = false;
    std::deque<Step> outputs;
    std::vector<Queued> inputs;
    int formatsGiven = 0;
    int releasedOutputs = 0;
    int id = 0;  // names its format
};

struct FakeFormat final : sink::TrackFormat {
    explicit FakeFormat(int id) : id(id) {}
    int id;
};

class FakeEncoder final : public sink::Encoder {
public:
    explicit FakeEncoder(std::shared_ptr<EncState> s) : s_(std::move(s)) {}
    ~FakeEncoder() override { s_->released = true; }

    bool configure(const sink::VideoFormat& f) override {
        s_->video = f;
        s_->configured = !s_->refuseConfigure;
        return s_->configured;
    }
    bool configure(const sink::AudioFormat& f) override {
        s_->audio = f;
        s_->configured = !s_->refuseConfigure;
        return s_->configured;
    }
    bool useSurfaceInput() override { return !s_->refuseSurface; }
    void* inputWindow() const override { return s_->refuseSurface ? nullptr : s_.get(); }
    bool start() override { return s_->started = true; }
    void stop() override { s_->stopped = true; }
    bool endSurfaceInput() override {
        ++s_->endedInput;
        if (s_->eosOnEndInput) s_->outputs.push_back(sample(0, 0, 0, sink::kFlagEndOfStream));
        return true;
    }
    int dequeueInput() override { return 7; }
    uint8_t* inputBuffer(int index, size_t* capacity) override {
        if (index != 7) return nullptr;
        in_.assign(s_->inputCapacity + 1, 0);
        *capacity = s_->inputCapacity;
        return in_.data() + (s_->unaligned ? 1 : 0);
    }
    bool queueInput(int index, size_t bytes, int64_t ptsUs, uint32_t flags) override {
        if (index != 7) return false;
        Queued q{bytes, ptsUs, flags, std::vector<int16_t>(bytes / sizeof(int16_t))};
        if (bytes > 0) std::memcpy(q.pcm.data(), in_.data() + (s_->unaligned ? 1 : 0), bytes);
        s_->inputs.push_back(std::move(q));
        if (s_->eosOnEosInput && (flags & sink::kFlagEndOfStream) != 0) {
            s_->outputs.push_back(sample(ptsUs, 0, 0, sink::kFlagEndOfStream));
        }
        return true;
    }
    Out dequeueOutput(int* index, BufferInfo* info) override {
        if (s_->errorNext) return Out::Error;
        if (s_->outputs.empty()) return Out::None;
        out_ = std::move(s_->outputs.front());
        s_->outputs.pop_front();
        if (out_.kind == Out::Sample) {
            *index = 3;
            *info = out_.info;
        }
        return out_.kind;
    }
    const uint8_t* outputBuffer(int index) override { return index == 3 ? out_.bytes.data() : nullptr; }
    bool releaseOutput(int index) override {
        ++s_->releasedOutputs;
        return index == 3;
    }
    std::unique_ptr<sink::TrackFormat> outputFormat() override {
        ++s_->formatsGiven;
        return std::make_unique<FakeFormat>(s_->id * 100 + s_->formatsGiven);
    }

private:
    std::shared_ptr<EncState> s_;
    std::vector<uint8_t> in_;
    Step out_;
};

struct Written {
    int track = 0;
    BufferInfo info;
    std::vector<uint8_t> bytes;
};

struct MuxState {
    std::vector<int> tracks;  // the format ids, in addTrack order
    bool started = false;
    bool stopped = false;
    bool writeBeforeStart = false;
    int failWriteAt = -1;
    std::vector<Written> written;
};

class FakeMuxer final : public sink::Muxer {
public:
    explicit FakeMuxer(std::shared_ptr<MuxState> s) : s_(std::move(s)) {}
    int addTrack(const sink::TrackFormat& format) override {
        if (s_->started) return -1;
        s_->tracks.push_back(static_cast<const FakeFormat&>(format).id);
        return static_cast<int>(s_->tracks.size()) - 1;
    }
    bool start() override { return s_->started = !s_->tracks.empty(); }
    bool writeSample(int track, const uint8_t* data, const BufferInfo& info) override {
        if (!s_->started) s_->writeBeforeStart = true;
        if (static_cast<int>(s_->written.size()) == s_->failWriteAt) return false;
        s_->written.push_back({track, info, std::vector<uint8_t>(data + info.offset, data + info.offset + info.size)});
        return true;
    }
    bool stop() override {
        s_->stopped = true;
        return s_->started;
    }

private:
    std::shared_ptr<MuxState> s_;
};

class FakeCodecs final : public sink::Codecs {
public:
    std::vector<std::shared_ptr<EncState>> queue;  // handed out in order, one per createEncoder
    std::shared_ptr<MuxState> mux = std::make_shared<MuxState>();
    std::vector<std::string> asked;
    // For each createEncoder: whether the previous rung's codec had been released by then.
    std::vector<bool> previousReleased;

    std::unique_ptr<sink::Encoder> createEncoder(const char* mime) override {
        asked.emplace_back(mime ? mime : "");
        if (made_ > 0) previousReleased.push_back(queue[made_ - 1]->released);
        if (made_ >= queue.size()) return nullptr;
        auto& s = queue[made_++];
        s->mime = mime;
        return std::make_unique<FakeEncoder>(s);
    }
    std::unique_ptr<sink::Muxer> createMuxer() override { return std::make_unique<FakeMuxer>(mux); }

private:
    size_t made_ = 0;
};

struct FakeHost final : sink::Host {
    bool end = false;
    int closing = 0;
    int encoded = 0;
    int64_t wroteBytes = 0;
    int failCount = 0;
    std::deque<int> pcm;  // frames given per takePcm, while any are left
    int64_t nextPtsUs = 0;
    int16_t nextValue = 1;

    bool ending() override { return end; }
    void inputClosing() override { ++closing; }
    int takePcm(int16_t* out, int capacityFrames, int64_t* ptsUs) override {
        if (pcm.empty()) return 0;
        const int frames = std::min(pcm.front(), capacityFrames);
        pcm.pop_front();
        for (int i = 0; i < frames * 2; ++i) out[i] = nextValue++;
        *ptsUs = nextPtsUs;
        nextPtsUs += static_cast<int64_t>(frames) * 1'000'000 / 48'000;
        return frames;
    }
    void videoEncoded() override { ++encoded; }
    void wrote(int64_t bytes) override { wroteBytes += bytes; }
    void failed() override { ++failCount; }
};

std::shared_ptr<EncState> encoder(int id) {
    auto s = std::make_shared<EncState>();
    s->id = id;
    return s;
}

sink::AudioFormat aac() {
    sink::AudioFormat a;
    a.sampleRateHz = 48'000;
    a.channels = 2;
    a.bitRate = 192'000;
    return a;
}

// Rungs 1440, 1080, 720 wide; counts how many were asked for.
struct Ladder {
    int asked = 0;
    sink::Rungs rungs() {
        return [this](int attempt, sink::VideoFormat* out) {
            constexpr int kWidths[] = {1440, 1080, 720};
            if (attempt < 0 || attempt >= 3) return false;
            ++asked;
            out->width = kWidths[attempt];
            out->height = kWidths[attempt] * 16 / 9;
            out->bitRate = 1'000'000 * (3 - attempt);
            out->frameRate = 30;
            return true;
        };
    }
};

// A sink opened on one video and one audio encoder, ready to step.
struct Rig {
    FakeCodecs codecs;
    FakeHost host;
    std::shared_ptr<EncState> video = encoder(1);
    std::shared_ptr<EncState> audio = encoder(2);
    std::unique_ptr<sink::Muxer> muxer;
    std::unique_ptr<EncodeSink> sink;
    Ladder ladder;

    Rig() {
        codecs.queue = {video, audio};
        muxer = codecs.createMuxer();
        sink = std::make_unique<EncodeSink>(codecs, *muxer, host);
    }
    bool open() { return sink->open(ladder.rungs(), aac()); }
    void run(int passes) {
        for (int i = 0; i < passes && !sink->done(); ++i) sink->step();
    }
};

bool ptsForward(const std::vector<Written>& written, int track) {
    int64_t last = INT64_MIN;
    for (const Written& w : written) {
        if (w.track != track) continue;
        if (w.info.ptsUs < last) return false;
        last = w.info.ptsUs;
    }
    return true;
}

void ladder() {
    {
        Rig r;
        auto refused = encoder(3);
        refused->refuseConfigure = true;
        auto noSurface = encoder(4);
        noSurface->refuseSurface = true;
        r.codecs.queue = {refused, noSurface, r.video, r.audio};
        check(r.open(), "ladder: the third rung opens");
        check(r.ladder.asked == 3, "ladder: no rung past the first that works");
        check(r.sink->video().width == 720, "ladder: the accepted rung is the one kept");
        check(r.video->video.width == 720 && r.video->video.bitRate == 1'000'000, "ladder: the kept codec got that rung");
        check(refused->video.width == 1440 && noSurface->video.width == 1080, "ladder: each rung tried in order");
        check(refused->released && noSurface->released, "ladder: refused codecs released");
        check(r.codecs.previousReleased.size() == 3 && r.codecs.previousReleased[0] && r.codecs.previousReleased[1],
              "ladder: a refused codec is released before the next is made");
        check(r.codecs.asked.size() == 4 && r.codecs.asked[3] == sink::kMimeAac, "ladder: then the AAC encoder");
        check(r.video->started && r.audio->started, "ladder: both encoders start");
        check(r.sink->inputWindow() == r.video.get(), "ladder: the kept codec's surface");
        check(r.video->video.colorFormat == sink::kColorFormatSurface && r.video->video.iFrameIntervalSec == 1 &&
                  std::string(r.video->video.mime) == sink::kMimeAvc,
              "ladder: surface input, a key frame a second, H.264");
        check(r.audio->audio.aacProfile == sink::kAacProfileLc && r.audio->audio.sampleRateHz == 48'000 &&
                  r.audio->audio.channels == 2 && r.audio->audio.bitRate == 192'000,
              "ladder: the audio format as asked");
    }
    {
        Rig r;
        auto a = encoder(3);
        auto b = encoder(4);
        auto c = encoder(5);
        a->refuseConfigure = b->refuseConfigure = c->refuseConfigure = true;
        r.codecs.queue = {a, b, c};
        check(!r.open(), "ladder: no rung works, nothing opens");
        check(r.ladder.asked == 3, "ladder: every rung tried");
        check(r.sink->inputWindow() == nullptr, "ladder: no surface");
    }
    {
        Rig r;
        r.audio->refuseConfigure = true;
        check(!r.open(), "ladder: an audio encoder that will not configure fails the open");
        check(!r.video->started, "ladder: nothing started then");
    }
}

void heldThenWritten() {
    Rig r;
    check(r.open(), "held: opens");
    r.video->outputs = {sample(0, 0x11, 30, sink::kFlagCodecConfig), formatChanged(), sample(0, 0xA0, 8, 1, 3),
                        sample(33'333, 0xA1, 9), sample(66'666, 0xA2, 10)};
    r.host.pcm = {1024};
    r.run(5);
    check(r.codecs.mux->written.empty() && !r.codecs.mux->started, "held: nothing written before every format");
    check(r.host.encoded == 3, "held: each video sample counted as it comes out, config left out");
    check(r.video->releasedOutputs == 4, "held: every buffer handed back");
    r.audio->outputs = {sample(0, 0x22, 2, sink::kFlagCodecConfig), formatChanged(), sample(0, 0xB0, 6)};
    r.run(3);
    const auto& mux = *r.codecs.mux;
    check(mux.started && !mux.writeBeforeStart, "held: the muxer starts before any write");
    check(mux.tracks.size() == 2 && mux.tracks[0] / 100 == 1 && mux.tracks[1] / 100 == 2, "held: video track first");
    check(mux.written.size() == 4, "held: held and new samples all written once");
    if (mux.written.size() == 4) {
        check(mux.written[0].info.ptsUs == 0 && mux.written[0].bytes == std::vector<uint8_t>(8, 0xA0) &&
                  mux.written[0].info.offset == 0 && mux.written[0].info.flags == 1,
              "held: the first sample's bytes without the codec's padding, its flags kept");
        check(mux.written[1].bytes == std::vector<uint8_t>(9, 0xA1) && mux.written[2].bytes == std::vector<uint8_t>(10, 0xA2),
              "held: in the order they came out");
        check(mux.written[3].track == 1 && mux.written[3].bytes == std::vector<uint8_t>(6, 0xB0), "held: then the audio");
    }
    check(r.host.wroteBytes == 8 + 9 + 10 + 6, "held: every written byte reported");
    for (const Written& w : mux.written) check((w.info.flags & sink::kFlagCodecConfig) == 0, "held: no codec-config in the file");
    check(r.audio->inputs.size() == 1 && r.audio->inputs[0].bytes == 1024 * 4 && r.audio->inputs[0].pcm[0] == 1,
          "held: the audio fed straight from the host");
}

void formatChangeMidStream() {
    Rig r;
    check(r.open(), "format change: opens");
    r.video->outputs = {formatChanged(), sample(0, 1)};
    r.audio->outputs = {formatChanged()};
    r.run(3);
    r.video->outputs = {formatChanged(), sample(33'333, 2)};
    r.run(3);
    const auto& mux = *r.codecs.mux;
    check(mux.tracks.size() == 2, "format change: no new track after the muxer started");
    check(mux.written.size() == 2 && mux.written[1].track == 0, "format change: still the video track");
    check(!r.sink->done(), "format change: still running");
}

void endOfStream() {
    Rig r;
    check(r.open(), "end: opens");
    r.video->eosOnEndInput = true;
    r.audio->eosOnEosInput = true;
    r.video->outputs = {formatChanged(), sample(0, 1), sample(33'333, 2)};
    r.audio->outputs = {formatChanged(), sample(0, 3), sample(21'333, 4)};
    r.host.pcm = {4096, 4096, 100};
    r.run(6);
    check(r.host.closing == 0 && r.video->endedInput == 0, "end: nothing ends before the host says");
    r.host.end = true;
    r.host.pcm = {500};
    r.run(50);
    check(r.sink->done() && !r.sink->failed(), "end: both streams end");
    check(r.host.closing == 1 && r.video->endedInput == 1, "end: the tee closes once, then the surface ends");
    const auto& in = r.audio->inputs;
    check(in.size() == 5, "end: the rest of the audio, then its end");
    if (in.size() == 5) {
        check(in[3].bytes == 500 * 4 && in[3].flags == 0, "end: what was left after the stop");
        check(in[4].bytes == 0 && (in[4].flags & sink::kFlagEndOfStream) != 0, "end: the first empty answer ends the audio");
        check(in[4].ptsUs == in[3].ptsUs, "end: the end keeps the last PTS the host gave");
    }
    check(r.sink->finish(), "end: a finished video");
    check(r.codecs.mux->stopped && r.video->stopped && r.audio->stopped, "end: muxer and both encoders stopped");
    check(ptsForward(r.codecs.mux->written, 0) && ptsForward(r.codecs.mux->written, 1), "end: PTS never go backwards");
    check(r.codecs.mux->written.size() == 4, "end: an empty end-of-stream buffer is not a sample");
    check(!r.sink->step(), "end: nothing moves once done");
}

void pcmBuffers() {
    {
        Rig r;
        r.audio->inputCapacity = 1001;  // 250 whole frames
        check(r.open(), "pcm: opens");
        r.host.pcm = {4096};
        r.run(1);
        check(r.audio->inputs.size() == 1 && r.audio->inputs[0].bytes == 250 * 4, "pcm: no more than the buffer holds");
    }
    {
        Rig r;
        r.audio->unaligned = true;
        check(r.open(), "pcm unaligned: opens");
        r.host.pcm = {16};
        r.run(1);
        bool same = r.audio->inputs.size() == 1 && r.audio->inputs[0].pcm.size() == 32;
        for (size_t i = 0; same && i < 32; ++i) same = r.audio->inputs[0].pcm[i] == static_cast<int16_t>(i + 1);
        check(same, "pcm unaligned: copied in whole");
    }
    {
        Rig r;
        check(r.open(), "pcm idle: opens");
        r.run(3);
        check(r.audio->inputs.empty(), "pcm idle: nothing queued while nothing is due");
        r.host.pcm = {128};
        r.run(1);
        check(r.audio->inputs.size() == 1 && r.audio->inputs[0].bytes == 128 * 4, "pcm idle: the kept buffer used next");
        check(r.sink->kPcmFrames == 4096, "pcm: 4096 frames a buffer at most");
    }
}

void errors() {
    {
        Rig r;
        check(r.open(), "error: opens");
        r.video->outputs = {formatChanged(), sample(0, 1)};
        r.audio->outputs = {formatChanged()};
        r.run(3);
        r.video->errorNext = true;
        r.sink->step();
        check(r.sink->done() && r.sink->failed(), "error: a codec error ends it");
        check(r.host.failCount == 1 && r.host.closing == 1, "error: the host hears the failure and the tee closes");
        check(!r.sink->finish(), "error: not a finished video");
        check(r.codecs.mux->stopped, "error: the file is still closed off");
    }
    {
        Rig r;
        check(r.open(), "write error: opens");
        r.codecs.mux->failWriteAt = 1;
        r.video->outputs = {formatChanged(), sample(0, 1), sample(33'333, 2), sample(66'666, 3)};
        r.audio->outputs = {formatChanged()};
        r.run(10);
        check(r.sink->failed() && r.host.failCount == 1, "write error: a refused sample ends it");
        check(r.codecs.mux->written.size() == 1, "write error: nothing after it");
    }
    {
        Rig r;
        check(r.open(), "no video: opens");
        r.video->eosOnEndInput = true;
        r.audio->eosOnEosInput = true;
        r.video->outputs = {formatChanged()};
        r.audio->outputs = {formatChanged(), sample(0, 9)};
        r.host.end = true;
        r.run(20);
        check(r.sink->done() && !r.sink->failed(), "no video: ends");
        check(!r.sink->finish() && !r.codecs.mux->stopped, "no video: not a video, and the muxer is never stopped");
    }
}

void exactlyOnce() {
    Rig r;
    check(r.open(), "once: opens");
    r.video->eosOnEndInput = true;
    r.audio->eosOnEosInput = true;
    std::vector<int64_t> made;
    for (int i = 0; i < 40; ++i) {
        r.video->outputs.push_back(sample(i * 33'333, static_cast<uint8_t>(i), 5 + i % 7));
        made.push_back(i * 33'333);
        if (i == 5) r.audio->outputs.push_back(formatChanged());
        if (i == 0) r.video->outputs.push_front(formatChanged());
        if (i % 3 == 0) r.video->outputs.push_back(sample(i * 33'333, 0x55, 4, sink::kFlagCodecConfig));
    }
    for (int i = 0; i < 20; ++i) r.audio->outputs.push_back(sample(i * 21'333, static_cast<uint8_t>(100 + i), 4));
    r.run(15);
    r.host.end = true;
    r.run(200);
    const auto& mux = *r.codecs.mux;
    std::vector<int64_t> videoPts;
    int audio = 0;
    for (const Written& w : mux.written) {
        if (w.track == 0 && w.info.size > 0) videoPts.push_back(w.info.ptsUs);
        if (w.track == 1) ++audio;
    }
    check(videoPts == made, "once: every video sample written exactly once, in order");
    check(audio == 20, "once: every audio sample written exactly once");
    check(r.host.encoded == 40, "once: each video sample counted once");
    check(ptsForward(mux.written, 0) && ptsForward(mux.written, 1), "once: PTS never go backwards per track");
    check(r.sink->samplesWritten() == static_cast<int64_t>(mux.written.size()), "once: the count matches the file");
}

// Record's session: the recorder's rungs and formats, the stop through the tee, the tee's timeout.
struct SessionRig {
    std::shared_ptr<record::Recorder> recorder =
        std::make_shared<record::Recorder>(record::Config{1080, 2400, 30, 0, 0});
    std::unique_ptr<FakeCodecs> codecs = std::make_unique<FakeCodecs>();
    std::shared_ptr<EncState> video = encoder(1);
    std::shared_ptr<EncState> audio = encoder(2);
    std::shared_ptr<MuxState> mux = codecs->mux;
    std::atomic<int64_t> clock{5'000'000'000};

    SessionRig() {
        video->eosOnEndInput = true;
        audio->eosOnEosInput = true;
        video->outputs = {formatChanged(), sample(0, 1), sample(33'333, 2)};
        audio->outputs = {formatChanged()};
    }
    std::unique_ptr<record::Session> open() {
        return record::Session::open(recorder, std::move(codecs), 1080, 2400, 1080, 30,
                                     [this] { return clock.fetch_add(1'000'000); });
    }
};

bool waitFor(const record::Session& s, record::Session::State want, int ms) {
    for (int i = 0; i < ms; ++i) {
        if (s.state() == want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return s.state() == want;
}

void session() {
    {
        SessionRig r;
        auto refused = encoder(3);
        refused->refuseConfigure = true;
        r.codecs->queue = {refused, r.video, r.audio};
        auto s = r.open();
        check(s != nullptr, "session: opens on the second rung");
        if (!s) return;
        check(s->width() == 720 && s->height() == 1600, "session: the 720 rung after 1080 was refused");
        check(refused->video.width == 1080 && refused->video.height == 2400, "session: the stage's own size first");
        check(r.video->video.bitRate == record::videoBps(720, 1600, 30, 0) && r.video->video.frameRate == 30 &&
                  r.video->video.iFrameIntervalSec == 1 && r.video->video.colorFormat == sink::kColorFormatSurface,
              "session: the recorder's bit rate, the asked fps, a key frame a second");
        check(r.audio->audio.sampleRateHz == record::Recorder::kAudioRateHz &&
                  r.audio->audio.channels == record::Recorder::kChannels && r.audio->audio.bitRate == encode::kAacBps &&
                  r.audio->audio.aacProfile == sink::kAacProfileLc,
              "session: 48 kHz stereo AAC-LC at the plan's rate");
        bool window = false;
        check(s->withInputWindow([&](void* w) {
                  window = w == static_cast<void*>(r.video.get());
                  return window;
              }) && window,
              "session: the encoder's surface while it runs");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        check(s->state() == record::Session::State::Running, "session: runs until the clock stops");
        r.recorder->stop(record::Stop::User, r.clock.load());
        check(waitFor(*s, record::Session::State::Closing, 500), "session: asks for the tee to close");
        check(r.video->endedInput == 0, "session: the video stream waits for the tee");
        s->teeClosed();
        check(waitFor(*s, record::Session::State::Saved, 500), "session: a saved video once the tee closed");
        check(r.video->endedInput == 1 && r.mux->stopped, "session: the stream ended and the file finished");
        check(r.video->released && r.audio->released, "session: the encoders released at the end");
        check(!s->withInputWindow([](void*) { return true; }), "session: no surface once released");
        check(r.mux->written.size() == 2 && r.mux->tracks.size() == 2, "session: both video samples in the file");
        check(r.recorder->stopped() == record::Stop::User, "session: the user's stop stands");
    }
    {
        SessionRig r;
        r.codecs->queue = {r.video, r.audio};
        auto s = r.open();
        check(s != nullptr, "session timeout: opens");
        if (!s) return;
        const auto from = std::chrono::steady_clock::now();
        r.recorder->stop(record::Stop::MaxLength, r.clock.load());
        check(waitFor(*s, record::Session::State::Saved, 3'000), "session timeout: finishes without the tee");
        const auto waited = std::chrono::steady_clock::now() - from;
        check(waited >= std::chrono::milliseconds(record::Session::kTeeWaitMs - 50),
              "session timeout: after waiting for the tee");
    }
    {
        SessionRig r;
        r.video->errorNext = true;
        r.codecs->queue = {r.video, r.audio};
        auto s = r.open();
        check(s != nullptr, "session error: opens");
        if (!s) return;
        check(waitFor(*s, record::Session::State::Closing, 500), "session error: asks for the tee to close");
        check(r.recorder->stopped() == record::Stop::Failed, "session error: the clock stops as failed");
        s->teeClosed();
        check(waitFor(*s, record::Session::State::Failed, 500), "session error: not saved");
    }
    {
        SessionRig r;
        auto a = encoder(3);
        auto b = encoder(4);
        a->refuseConfigure = b->refuseConfigure = true;
        r.codecs->queue = {a, b};
        check(r.open() == nullptr, "session: no rung of 1080 or 720 opens, no session");
        check(a->released && b->released, "session: the refused codecs released");
    }
    {
        SessionRig r;
        r.codecs->queue = {r.video, r.audio};
        auto s = r.open();
        check(s != nullptr, "session destroyed early: opens");
        s.reset();
        check(r.recorder->stopped() == record::Stop::Failed, "session destroyed while running: stops as failed");
    }
}

}  // namespace

int main() {
    ladder();
    heldThenWritten();
    formatChangeMidStream();
    endOfStream();
    pcmBuffers();
    errors();
    exactlyOnce();
    session();
    if (failures == 0) std::printf("encode_sink_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
