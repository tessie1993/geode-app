// Host checks for an export's soundtrack transcode (core/output/AacTranscode) on a fake decoder, encoder and file:
// the AIFF and decoder paths, the start skip and trim, the range's end, the fold to two channels, float input, the
// resample to a forced rate, the sample table and duration, and each failure. Built and run by CI
// (.github/workflows/native-host-tests.yml, "AAC transcode host test"); no device.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "audio/dsp/PcmTiming.hpp"
#include "audio/ingest/AiffReader.hpp"
#include "audio/ingest/TrackPump.hpp"
#include "output/AacTranscode.hpp"
#include "output/EncodeSink.hpp"

using namespace geode::output;
using geode::audio::ingest::AiffReader;
using geode::audio::ingest::DeclaredTrack;
using geode::audio::ingest::DecodedChunk;
using geode::audio::ingest::DecodePort;
using geode::audio::ingest::MemorySource;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Queued {
    size_t bytes = 0;
    int64_t ptsUs = 0;
    uint32_t flags = 0;
    int16_t first = 0;
};

struct EncState {
    sink::AudioFormat audio;
    bool configured = false;
    bool started = false;
    bool released = false;
    bool noInput = false;     // never an input buffer: a stall
    bool noFormat = false;    // never names its format
    size_t capacity = 4096;
    std::vector<Queued> inputs;
};

class FakeFormat final : public sink::TrackFormat {};

// Each queued input comes out as one 10-byte access unit at its PTS; the end of stream as an empty flagged buffer.
class FakeEncoder final : public sink::Encoder {
public:
    explicit FakeEncoder(std::shared_ptr<EncState> s) : s_(std::move(s)) {}
    ~FakeEncoder() override { s_->released = true; }

    bool configure(const sink::VideoFormat&) override { return false; }
    bool configure(const sink::AudioFormat& f) override {
        s_->audio = f;
        return s_->configured = true;
    }
    bool useSurfaceInput() override { return false; }
    void* inputWindow() const override { return nullptr; }
    bool start() override { return s_->started = true; }
    void stop() override {}
    bool endSurfaceInput() override { return false; }
    int dequeueInput() override { return s_->noInput ? kNoInput : 0; }
    uint8_t* inputBuffer(int, size_t* capacity) override {
        in_.assign(s_->capacity, 0);
        *capacity = s_->capacity;
        return in_.data();
    }
    bool queueInput(int, size_t bytes, int64_t ptsUs, uint32_t flags) override {
        Queued q{bytes, ptsUs, flags, 0};
        if (bytes >= 2) std::memcpy(&q.first, in_.data(), 2);
        s_->inputs.push_back(q);
        if (!formatSent_ && !s_->noFormat) {
            pending_.push_back({-1, {}});
            formatSent_ = true;
        }
        if ((flags & sink::kFlagEndOfStream) != 0) {
            pending_.push_back({0, {0, ptsUs, sink::kFlagEndOfStream}});
        } else {
            pending_.push_back({0, {10, ptsUs, 0}});
        }
        return true;
    }
    sink::Out dequeueOutput(int* index, sink::BufferInfo* info) override {
        if (pending_.empty()) return sink::Out::None;
        const auto next = pending_.front();
        pending_.erase(pending_.begin());
        if (next.first < 0) return sink::Out::FormatChanged;
        *index = 0;
        *info = next.second;
        return sink::Out::Sample;
    }
    const uint8_t* outputBuffer(int, size_t* capacity) override {
        *capacity = sizeof(out_);
        return out_;
    }
    bool releaseOutput(int) override { return true; }
    std::unique_ptr<sink::TrackFormat> outputFormat() override { return std::make_unique<FakeFormat>(); }

private:
    std::shared_ptr<EncState> s_;
    std::vector<uint8_t> in_;
    std::vector<std::pair<int, sink::BufferInfo>> pending_;
    bool formatSent_ = false;
    uint8_t out_[16] = {};
};

class FakeHost final : public aac::Host {
public:
    std::shared_ptr<EncState> enc = std::make_shared<EncState>();
    bool noEncoder = false;
    bool failWrite = false;
    bool failFinish = false;
    bool finished = false;
    int idles = 0;
    std::vector<uint8_t> file;

    std::unique_ptr<sink::Encoder> createEncoder() override {
        if (noEncoder) return nullptr;
        return std::make_unique<FakeEncoder>(enc);
    }
    bool write(const uint8_t* data, size_t bytes) override {
        if (failWrite) return false;
        file.insert(file.end(), data, data + bytes);
        return true;
    }
    bool finish() override {
        finished = true;
        return !failFinish;
    }
    void idleWait() override { ++idles; }
};

struct Buffer {
    std::vector<uint8_t> bytes;
    bool isFloat = false;
    int channels = 2;
    int rate = 48'000;
    int64_t ptsUs = 0;
};

class FakePort final : public DecodePort {
public:
    explicit FakePort(std::vector<Buffer> buffers) : buffers_(std::move(buffers)) {}
    DeclaredTrack declaredTrack;
    bool throwOnOutput = false;
    int* released = nullptr;

    DeclaredTrack declared() const override { return declaredTrack; }
    bool feedInput() override { return ++fed_ >= 2; }
    bool takeOutput(DecodedChunk& out) override {
        if (throwOnOutput) throw std::runtime_error("codec error");
        if (next_ >= buffers_.size()) return false;
        const Buffer& b = buffers_[next_++];
        out = {};
        out.data = b.bytes.data();
        out.bytes = b.bytes.size();
        out.isFloat = b.isFloat;
        out.channels = b.channels;
        out.sampleRate = b.rate;
        out.ptsUs = b.ptsUs;
        out.endOfStream = next_ == buffers_.size();
        return true;
    }
    void releaseOutput() override {
        if (released) ++*released;
    }

private:
    std::vector<Buffer> buffers_;
    size_t next_ = 0;
    int fed_ = 0;
};

// frames of s16 at `channels`, every sample of frame f holding base + f.
Buffer s16(int frames, int channels, int rate, int64_t ptsUs, int base) {
    Buffer b;
    b.channels = channels;
    b.rate = rate;
    b.ptsUs = ptsUs;
    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < channels; ++c) {
            const auto v = static_cast<int16_t>(base + f);
            const auto* p = reinterpret_cast<const uint8_t*>(&v);
            b.bytes.insert(b.bytes.end(), p, p + 2);
        }
    }
    return b;
}

void be16(std::vector<uint8_t>& b, int v) {
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void be32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// A mono 16-bit AIFF at `rate`: frame f holds f % 30000.
std::vector<uint8_t> aiffBytes(uint32_t rate, uint32_t frames) {
    std::vector<uint8_t> body;
    tag(body, "AIFF");
    tag(body, "COMM");
    be32(body, 18);
    be16(body, 1);
    be32(body, frames);
    be16(body, 16);
    int exponent = 16383 + 31;
    uint32_t m = rate;
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

std::unique_ptr<AiffReader> aiff(uint32_t rate, uint32_t frames) {
    return AiffReader::open(std::make_unique<MemorySource>(aiffBytes(rate, frames)));
}

void run(aac::Transcode& job) {
    for (int i = 0; i < 1'000'000 && job.step(); ++i) {
    }
}

size_t pcmBytes(const EncState& s) {
    size_t n = 0;
    for (const Queued& q : s.inputs) n += q.bytes;
    return n;
}

void aiffWhole() {
    FakeHost host;
    aac::Transcode job({0, 0, 0}, aiff(8000, 8000 * 3), host);
    run(job);
    check(job.state() == aac::State::Done, "aiff: done");
    check(host.enc->audio.sampleRateHz == 8000 && host.enc->audio.channels == 1, "aiff: encoder at the file's rate, mono");
    check(host.enc->audio.bitRate == aac::kBitRate, "aiff: the bit rate");
    check(pcmBytes(*host.enc) == 8000 * 3 * 2, "aiff: every sample fed");
    int64_t fed = 0;
    bool stamped = true;
    for (const Queued& q : host.enc->inputs) {
        if (q.bytes == 0) continue;
        stamped = stamped && q.ptsUs == geode::audio::s16BytesToUs(fed, 8000, 1);
        fed += static_cast<int64_t>(q.bytes);
    }
    check(stamped, "aiff: each input stamped by the bytes fed before it");
    check((host.enc->inputs.back().flags & sink::kFlagEndOfStream) != 0, "aiff: end of stream last");
    const auto units = host.enc->inputs.size() - 1;
    check(job.samples().size() == units && host.file.size() == units * 10, "aiff: one table row per access unit");
    check(job.samples()[1].offset == 10 && job.samples()[1].size == 10, "aiff: rows point into the file");
    check(job.durationUs() == geode::audio::aacEndUs(job.samples().back().ptsUs, 8000), "aiff: duration one frame past");
    check(job.progress() == 1.0f && host.finished && host.enc->released == false, "aiff: progress full, file finished");
}

void aiffRange() {
    FakeHost host;
    aac::Transcode job({1000, 1000, 0}, aiff(8000, 8000 * 3), host);
    run(job);
    check(job.state() == aac::State::Done, "aiff range: done");
    check(host.enc->inputs.front().first == 8000, "aiff range: starts at the frame of 1 s");
    const size_t bytes = pcmBytes(*host.enc);
    check(bytes >= 8000 * 2 && bytes <= 8000 * 2 + 2 * static_cast<size_t>(aac::kAiffReadSamples) * 2,
          "aiff range: about a second fed, stopped past it");
}

void aiffResampled() {
    FakeHost host;
    aac::Transcode job({0, 0, 48'000}, aiff(8000, 8000), host);
    run(job);
    check(job.state() == aac::State::Done && job.sampleRate() == 48'000, "aiff forced rate: encoder at 48 kHz");
    const size_t frames = pcmBytes(*host.enc) / 2;
    check(frames > 47'000 && frames <= 48'000, "aiff forced rate: a second in is about a second out");
}

void decoderStereo() {
    FakeHost host;
    std::vector<Buffer> buffers;
    for (int i = 0; i < 4; ++i) buffers.push_back(s16(1024, 2, 44'100, i * 23'219, i * 1024));
    int released = 0;
    auto port = std::make_unique<FakePort>(std::move(buffers));
    port->released = &released;
    aac::Transcode job({0, 0, 0}, std::move(port), host);
    run(job);
    check(job.state() == aac::State::Done, "decoder: done");
    check(host.enc->audio.sampleRateHz == 44'100 && host.enc->audio.channels == 2, "decoder: rate and channels");
    check(pcmBytes(*host.enc) == 4 * 1024 * 4, "decoder: every frame fed");
    check(host.enc->inputs[0].ptsUs == 0, "decoder: first input at 0");
    check(released == 4, "decoder: every buffer handed back");
}

void decoderFold() {
    FakeHost host;
    std::vector<Buffer> buffers{s16(512, 6, 48'000, 0, 100)};
    aac::Transcode job({0, 0, 0}, std::make_unique<FakePort>(std::move(buffers)), host);
    run(job);
    check(job.state() == aac::State::Done && host.enc->audio.channels == 2, "5.1: folded to stereo");
    check(pcmBytes(*host.enc) == 512 * 4, "5.1: two channels of every frame fed");
}

void decoderFloat() {
    FakeHost host;
    Buffer b;
    b.isFloat = true;
    b.channels = 1;
    for (int i = 0; i < 256; ++i) {
        const float v = 0.5f;
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        b.bytes.insert(b.bytes.end(), p, p + sizeof(float));
    }
    aac::Transcode job({0, 0, 0}, std::make_unique<FakePort>(std::vector<Buffer>{b}), host);
    run(job);
    check(job.state() == aac::State::Done && pcmBytes(*host.enc) == 256 * 2, "float: converted to s16");
    check(host.enc->inputs[0].first == 16383, "float: 0.5 is 16383");
}

void decoderTrim() {
    FakeHost host;
    // A buffer from the sync sample 10 ms before the start, then one after it.
    std::vector<Buffer> buffers{s16(1024, 2, 48'000, 0, 0), s16(1024, 2, 48'000, 21'333, 1024)};
    aac::Transcode job({10, 0, 0}, std::make_unique<FakePort>(std::move(buffers)), host);
    run(job);
    check(job.state() == aac::State::Done, "trim: done");
    check(host.enc->inputs[0].ptsUs == 0 && host.enc->inputs[0].first == 480, "trim: the start's frame at 0");
    check(pcmBytes(*host.enc) == (1024 - 480) * 4 + 1024 * 4, "trim: the frames before the start dropped");
}

void decoderRangeEnd() {
    FakeHost host;
    std::vector<Buffer> buffers;
    for (int i = 0; i < 10; ++i) buffers.push_back(s16(4800, 1, 48'000, i * 100'000, 0));
    int released = 0;
    auto port = std::make_unique<FakePort>(std::move(buffers));
    port->released = &released;
    aac::Transcode job({0, 300, 0}, std::move(port), host);
    run(job);
    check(job.state() == aac::State::Done, "range end: done");
    check(pcmBytes(*host.enc) == 4 * 4800 * 2, "range end: the buffers to 300 ms, none past");
    check(released == 5, "range end: the one past the end handed back too");
}

void decoderSilent() {
    FakeHost host;
    Buffer eos;
    eos.ptsUs = 0;
    auto port = std::make_unique<FakePort>(std::vector<Buffer>{eos});
    port->declaredTrack = {1'000'000, 96'000, 6};
    aac::Transcode job({0, 0, 0}, std::move(port), host);
    run(job);
    check(job.state() == aac::State::Done, "no audio out: still a track");
    check(host.enc->audio.sampleRateHz == 48'000 && host.enc->audio.channels == 2, "no audio out: the declared shape");
    check(job.samples().empty() && job.durationUs() == 0, "no audio out: empty");
}

void failuresOf() {
    {
        FakeHost host;
        aac::Transcode job({0, 0, 0}, std::unique_ptr<DecodePort>{}, host);
        check(!job.step() && job.failure() == aac::Failure::NoAudio, "no port, the source read: no audio");
        check(job.detail() == "No audio track in source file", "no audio: its words");
    }
    {
        FakeHost host;
        aac::Transcode job({0, 0, 0}, std::unique_ptr<DecodePort>{}, host, true);
        check(!job.step() && job.failure() == aac::Failure::Unreadable, "no port, the source unreadable: unreadable");
        check(host.enc->inputs.empty() && !host.enc->configured, "unreadable: no encoder opened");
    }
    {
        FakeHost host;
        aac::Transcode job({0, 0, 0}, std::unique_ptr<AiffReader>{}, host);
        check(!job.step() && job.failure() == aac::Failure::NoAudio, "no reader: no audio");
    }
    {
        FakeHost host;
        host.noEncoder = true;
        aac::Transcode job({0, 0, 0}, aiff(8000, 800), host);
        run(job);
        check(job.failure() == aac::Failure::Codec, "no encoder: codec");
    }
    {
        FakeHost host;
        host.failWrite = true;
        aac::Transcode job({0, 0, 0}, aiff(8000, 8000), host);
        run(job);
        check(job.failure() == aac::Failure::Write, "a full disk: write");
    }
    {
        FakeHost host;
        host.failFinish = true;
        aac::Transcode job({0, 0, 0}, aiff(8000, 800), host);
        run(job);
        check(job.failure() == aac::Failure::Write, "the last bytes not written: write");
    }
    {
        FakeHost host;
        host.enc->noFormat = true;
        aac::Transcode job({0, 0, 0}, aiff(8000, 800), host);
        run(job);
        check(job.failure() == aac::Failure::NoFormat, "no format: refused");
    }
    {
        FakeHost host;
        host.enc->noInput = true;
        aac::Transcode job({0, 0, 0}, aiff(8000, 800), host);
        run(job);
        check(job.failure() == aac::Failure::Stalled && host.idles >= aac::kMaxIdleSteps, "no input buffer: stalled");
    }
    {
        FakeHost host;
        auto port = std::make_unique<FakePort>(std::vector<Buffer>{s16(16, 1, 48'000, 0, 0)});
        port->throwOnOutput = true;
        aac::Transcode job({0, 0, 0}, std::move(port), host);
        run(job);
        check(job.failure() == aac::Failure::Codec && job.detail() == "codec error", "a codec throw: codec, its words");
    }
}

// The track's format as a muxer takes it whole: the keys the shell used to copy out of its MediaFormat.
void formatKeys() {
    const uint8_t csd[2] = {0x12, 0x10};
    const std::vector<sink::FormatEntry> e = aac::formatEntries(44'100, 2, aac::kBitRate, csd, 2);
    check(e.size() == 5, "a format with csd carries five keys");
    check(e.size() == 5 && e[0].key == "mime" && e[0].type == sink::kFormatString && e[0].text == sink::kMimeAac,
          "mime first, AAC");
    check(e.size() == 5 && e[1].key == "sample-rate" && e[1].type == sink::kFormatInt32 && e[1].whole == 44'100,
          "the rate");
    check(e.size() == 5 && e[2].key == "channel-count" && e[2].whole == 2, "the channels");
    check(e.size() == 5 && e[3].key == "bitrate" && e[3].whole == aac::kBitRate, "the bit rate");
    check(e.size() == 5 && e[4].key == "csd-0" && e[4].type == sink::kFormatBuffer && e[4].bytes.size() == 2 &&
              e[4].bytes[0] == 0x12,
          "csd-0 as a buffer");
    check(aac::formatEntries(48'000, 1, aac::kBitRate, nullptr, 0).size() == 4, "no csd, no csd key");
}

}  // namespace

int main() {
    formatKeys();
    aiffWhole();
    aiffRange();
    aiffResampled();
    decoderStereo();
    decoderFold();
    decoderFloat();
    decoderTrim();
    decoderRangeEnd();
    decoderSilent();
    failuresOf();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("aac_transcode_test: all passed\n");
    return 0;
}
