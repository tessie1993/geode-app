// Host test for the loop extend's copy (core/output/LoopMux): which samples of which loop land in the file once per
// repeat, their shifted timestamps and keyframe flags, the last repeat cut at the running time, the soundtrack
// interleaved by timestamp as the repeats go in, and what a cancel and each failure leave behind. The extractor, the
// scratch file and the muxer are fakes of the job's seams (LoopReader, Host, and the encode sink's Muxer).
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "output/LoopExtend.hpp"
#include "output/LoopMux.hpp"

namespace {

namespace lm = geode::output::loopmux;
namespace sink = geode::output::sink;
namespace ve = geode::output::videoexport;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kVideoTrack = 0;
constexpr int kAudioTrack = 1;
constexpr int kLoopSamples = 10;

struct Write {
    int track = -1;
    int64_t ptsUs = 0;
    int32_t size = 0;
    uint32_t flags = 0;
    int first = 0;  // the sample's first byte: loop * 16 + its index (video), 100 + clip (audio)
};

struct World {
    // Script.
    int noLoop = -1;           // this loop cannot be opened
    int mismatchLoop = -1;     // this loop carries another codec configuration
    bool failAudioRead = false;
    bool noMuxer = false;
    bool refuseStart = false;
    int failWriteAt = -1;      // the muxer refuses its Nth sample
    int cancelAtRewind = -1;   // the job is cancelled as the loop is rewound for its Nth repeat
    lm::Job* job = nullptr;

    // Record.
    std::vector<Write> writes;
    int starts = 0;
    int stops = 0;
    int rewinds = 0;
};

class FakeFormat final : public sink::TrackFormat {};

class FakeMuxer final : public sink::Muxer {
public:
    explicit FakeMuxer(World& world) : w_(world) {}
    int addTrack(const sink::TrackFormat&) override { return tracks_++; }
    bool start() override {
        if (w_.refuseStart) return false;
        ++w_.starts;
        return true;
    }
    bool writeSample(int track, const uint8_t* data, const sink::BufferInfo& info) override {
        if (w_.failWriteAt >= 0 && static_cast<int>(w_.writes.size()) == w_.failWriteAt) return false;
        w_.writes.push_back({track, info.ptsUs, info.size, info.flags, info.size > 0 ? data[0] : 0});
        return true;
    }
    bool stop() override {
        ++w_.stops;
        return true;
    }

private:
    World& w_;
    int tracks_ = 0;
};

class FakeCodecs final : public sink::Codecs {
public:
    explicit FakeCodecs(World& world) : w_(world) {}
    std::unique_ptr<sink::Encoder> createEncoder(const char*) override { return nullptr; }
    std::unique_ptr<sink::Muxer> createMuxer() override {
        return w_.noMuxer ? nullptr : std::make_unique<FakeMuxer>(w_);
    }
    std::unique_ptr<sink::TrackFormat> trackFormat(const std::vector<sink::FormatEntry>&) override {
        return std::make_unique<FakeFormat>();
    }

private:
    World& w_;
};

// Ten frames, one every 100 ms, a keyframe on the first and the fifth; each read gives its loop and index.
class FakeLoop final : public lm::LoopReader {
public:
    FakeLoop(World& world, int index) : w_(world), index_(index) {
        shape_.mime = "video/avc";
        shape_.width = 1920;
        shape_.height = 1080;
        shape_.csd0 = {1, 2};
        if (index == world.mismatchLoop) shape_.csd0 = {9};
    }
    const lm::Signature& signature() const override { return shape_; }
    int declaredBytes() const override { return 0; }
    std::unique_ptr<sink::TrackFormat> trackFormat() override { return std::make_unique<FakeFormat>(); }
    bool scan(std::vector<int64_t>* timesUs, std::vector<int>* flags) override {
        for (int i = 0; i < kLoopSamples; ++i) {
            timesUs->push_back(i * 100'000);
            flags->push_back(i == 0 || i == 4 ? 1 : 0);
        }
        return true;
    }
    bool rewind() override {
        head_ = 0;
        ++w_.rewinds;
        if (w_.rewinds == w_.cancelAtRewind && w_.job) w_.job->cancel();
        return true;
    }
    int next(uint8_t* out, size_t capacity) override {
        if (head_ >= kLoopSamples) return -1;
        const int size = 4 + head_;
        if (static_cast<size_t>(size) > capacity) return -1;
        out[0] = static_cast<uint8_t>(index_ * 16 + head_);
        ++head_;
        return size;
    }

private:
    World& w_;
    int index_;
    int head_ = 0;
    lm::Signature shape_;
};

class FakeHost final : public lm::Host {
public:
    explicit FakeHost(World& world) : w_(world) {}
    std::unique_ptr<sink::Codecs> codecs() override { return std::make_unique<FakeCodecs>(w_); }
    std::unique_ptr<lm::LoopReader> openLoop(int index, const char** why) override {
        if (index == w_.noLoop) {
            *why = "no video track";
            return nullptr;
        }
        return std::make_unique<FakeLoop>(w_, index);
    }
    int readAudio(int clip, const ve::AudioSample& sample, uint8_t* out) override {
        if (w_.failAudioRead) return -1;
        out[0] = static_cast<uint8_t>(100 + clip);
        return sample.size;
    }

private:
    World& w_;
};

// Two one-second loops and a soundtrack of two clips, three samples each, half a second apart in their clip.
lm::Request request(int64_t clipUs) {
    lm::Request r;
    r.loops = 2;
    r.loopDurationUs = 1'000'000;
    for (int c = 0; c < 2; ++c) {
        lm::Clip clip;
        clip.durationUs = clipUs;
        for (int i = 0; i < 3; ++i) clip.samples.push_back({0, 8, i * 500'000, 0});
        r.clips.push_back(clip);
    }
    sink::FormatEntry mime;
    mime.key = "mime";
    mime.type = sink::kFormatString;
    mime.text = sink::kMimeAac;
    r.audioFormat.push_back(mime);
    return r;
}

struct Run {
    explicit Run(const lm::Request& r) : host(world), job(r, host) { world.job = &job; }
    void go() { job.run(); }
    std::vector<Write> track(int which) const {
        std::vector<Write> out;
        for (const Write& w : world.writes) {
            if (w.track == which) out.push_back(w);
        }
        return out;
    }
    size_t indexOf(int which, int64_t ptsUs) const {
        for (size_t i = 0; i < world.writes.size(); ++i) {
            if (world.writes[i].track == which && world.writes[i].ptsUs == ptsUs) return i;
        }
        return world.writes.size();
    }
    World world;
    FakeHost host;
    lm::Job job;
};

void testWholeCopy() {
    // Three seconds of soundtrack over two one-second loops: three repeats, the first two of loop 0, the last of loop 1.
    Run run(request(1'500'000));
    check(run.job.state() == lm::State::Idle && run.job.progress() == geode::output::loopext::kAudioProgressSpan,
          "before the run: idle, at the end of the audio's share of the bar");
    run.go();
    const World& w = run.world;
    check(run.job.state() == lm::State::Done && run.job.failure() == lm::Failure::None, "the copy finishes");
    check(run.job.progress() == 1.0f, "the bar ends full");
    check(w.starts == 1 && w.stops == 1, "the muxer starts once and is finished once");
    const std::vector<Write> video = run.track(kVideoTrack);
    check(video.size() == 30, "every sample of every repeat is written");
    check(video.size() == 30 && video[0].ptsUs == 0 && video[9].ptsUs == 900'000 && video[10].ptsUs == 1'000'000 &&
              video[20].ptsUs == 2'000'000 && video[29].ptsUs == 2'900'000,
          "each repeat is shifted by a whole loop");
    check(video.size() == 30 && video[0].flags == 1 && video[1].flags == 0 && video[4].flags == 1 && video[10].flags == 1,
          "keyframes stay keyframes in every repeat");
    check(video.size() == 30 && video[0].first == 0 && video[10].first == 0 && video[20].first == 16 && video[29].first == 25,
          "the drift stops: loop 0 twice, then loop 1");
    check(video.size() == 30 && video[3].size == 7, "each sample is written whole");
    const std::vector<Write> audio = run.track(kAudioTrack);
    check(audio.size() == 6, "every soundtrack sample is written");
    check(audio.size() == 6 && audio[0].ptsUs == 0 && audio[2].ptsUs == 1'000'000 && audio[3].ptsUs == 1'500'000 &&
              audio[5].ptsUs == 2'500'000,
          "clips follow one another in the soundtrack");
    check(audio.size() == 6 && audio[0].first == 100 && audio[3].first == 101 && audio[2].size == 8,
          "each sample comes from its own clip, whole");
    // The soundtrack keeps pace: the samples before a repeat's end go in before the next repeat's first frame.
    check(run.indexOf(kAudioTrack, 500'000) < run.indexOf(kVideoTrack, 1'000'000), "audio before 1 s precedes the second repeat");
    check(run.indexOf(kAudioTrack, 1'000'000) > run.indexOf(kVideoTrack, 1'000'000) &&
              run.indexOf(kAudioTrack, 1'000'000) < run.indexOf(kVideoTrack, 2'000'000),
          "audio before 2 s follows the second repeat's start and precedes the third");
    check(run.indexOf(kAudioTrack, 2'500'000) > run.indexOf(kVideoTrack, 2'900'000), "the rest of the audio goes in last");
}

void testLastRepeatCut() {
    // 2.5 s of soundtrack: the third repeat stops where the file does.
    Run run(request(1'250'000));
    run.go();
    check(run.job.state() == lm::State::Done, "a partial last repeat finishes");
    const std::vector<Write> video = run.track(kVideoTrack);
    check(video.size() == 25, "the last repeat keeps only the samples before the running time");
    check(!video.empty() && video.back().ptsUs == 2'400'000, "its last sample is the one before the end");
    check(run.track(kAudioTrack).size() == 6, "the soundtrack is whole");
}

void testCancel() {
    Run run(request(1'500'000));
    run.world.cancelAtRewind = 2;
    run.go();
    check(run.job.state() == lm::State::Cancelled && run.job.failure() == lm::Failure::None, "a cancel ends the copy as one");
    check(run.track(kVideoTrack).size() == 20, "the repeat under way finishes and no later one starts");
    check(run.job.progress() < 1.0f, "the bar does not end full");
    check(run.world.stops == 1, "a muxer that took samples is stopped, the file the shell discards");
}

void testUnusable() {
    {
        Run run(request(1'500'000));
        run.world.mismatchLoop = 1;
        run.go();
        check(run.job.state() == lm::State::Failed && run.job.failure() == lm::Failure::Unusable &&
                  !run.job.detail().empty(),
              "loops not encoded alike cannot share a track");
        check(run.world.writes.empty() && run.world.starts == 0 && run.world.stops == 0, "nothing was written");
    }
    {
        Run run(request(1'500'000));
        run.world.noLoop = 0;
        run.go();
        check(run.job.failure() == lm::Failure::Unusable && run.job.detail() == "no video track",
              "a loop that cannot be opened fails in the host's words");
    }
    {
        lm::Request r = request(1'500'000);
        r.loops = 0;
        Run run(r);
        run.go();
        check(run.job.failure() == lm::Failure::Unusable && run.world.starts == 0, "no loops, no file");
    }
}

void testFailures() {
    {
        Run run(request(1'500'000));
        run.world.failAudioRead = true;
        run.go();
        check(run.job.state() == lm::State::Failed && run.job.failure() == lm::Failure::Read, "an unreadable sample is a read failure");
    }
    {
        Run run(request(1'500'000));
        run.world.failWriteAt = 3;
        run.go();
        check(run.job.state() == lm::State::Failed && run.job.failure() == lm::Failure::Write, "a refused sample is a write failure");
        check(run.world.writes.size() == 3, "the copy stops at the refusal");
    }
    {
        Run run(request(1'500'000));
        run.world.refuseStart = true;
        run.go();
        check(run.job.failure() == lm::Failure::Write && run.world.writes.empty(), "a muxer that will not start takes nothing");
    }
    {
        Run run(request(1'500'000));
        run.world.noMuxer = true;
        run.go();
        check(run.job.failure() == lm::Failure::Write, "no output file is a write failure");
    }
}

}  // namespace

int main() {
    testWholeCopy();
    testLastRepeatCut();
    testCancel();
    testUnusable();
    testFailures();
    if (failures == 0) std::printf("loop_mux_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
