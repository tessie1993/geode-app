// Host-side checks for an export's track pump (core/audio/ingest/TrackPump): the decode rule over a fake platform
// decoder (one sample in a step until end of stream is queued, one output buffer out, released once, the end
// after the buffer flagged end of stream), the pump's order (step until the rate is known, aim, hear, step
// whenever the held chunk runs out, false once the track ended), checked against that order written out
// step by step over the same chunks; the AIFF-first open over a real descriptor, its span and a second open
// of the same descriptor; and the small rules the Android decoder reads (seek, float output, the extractor's
// span, the audio track's mime). Built and run by CI (.github/workflows/native-host-tests.yml); no device.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/track_pump_test.cpp
//     core/audio/ingest/TrackPump.cpp core/audio/ingest/TrackFeed.cpp core/audio/ingest/AiffReader.cpp
//     core/output/FrameClock.cpp -o track_pump_test && ./track_pump_test
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include "audio/ingest/AiffReader.hpp"
#include "audio/ingest/TrackFeed.hpp"
#include "audio/ingest/TrackPump.hpp"

using namespace geode::audio::ingest;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// ── A fake decoder ───────────────────────────────────────────────────────────────────────────────────────

// One thing the codec's output side does on a dequeue: a buffer, or nothing ready yet.
struct Out {
    bool ready = true;
    std::vector<uint8_t> bytes;
    int64_t ptsUs = 0;
    bool eos = false;
};

struct PortLog {
    int inputs = 0;           // feedInput calls
    int inputsAfterEos = 0;   // feedInput calls after it returned true
    int taken = 0;            // buffers handed out
    int released = 0;         // buffers handed back
    bool releasedUntaken = false;
};

class FakePort final : public DecodePort {
public:
    FakePort(std::vector<Out> outs, int samplesIn, PortLog* log) : outs_(std::move(outs)), samplesIn_(samplesIn), log_(log) {}

    bool feedInput() override {
        if (eosQueued_) ++log_->inputsAfterEos;
        ++log_->inputs;
        // Every third call the codec has no input buffer free.
        if (log_->inputs % 3 == 0) return false;
        if (fed_ < samplesIn_) {
            ++fed_;
            return false;
        }
        eosQueued_ = true;
        return true;
    }

    bool takeOutput(DecodedChunk& out) override {
        if (next_ >= outs_.size()) return false;
        const Out& o = outs_[next_++];
        if (!o.ready) return false;
        out.data = o.bytes.empty() ? nullptr : o.bytes.data();
        out.bytes = o.bytes.size();
        out.isFloat = false;
        out.channels = 2;
        out.sampleRate = 44100;
        out.ptsUs = o.ptsUs;
        out.endOfStream = o.eos;
        holding_ = true;
        ++log_->taken;
        return true;
    }

    void releaseOutput() override {
        if (!holding_) log_->releasedUntaken = true;
        holding_ = false;
        ++log_->released;
    }

private:
    std::vector<Out> outs_;
    size_t next_ = 0;
    int samplesIn_;
    int fed_ = 0;
    bool eosQueued_ = false;
    bool holding_ = false;
    PortLog* log_;
};

// Stereo 16-bit chunks of `frames` frames, a ramp so every frame is told apart, from the sync frame `firstFrame`.
// A not-ready dequeue after every second buffer; the last buffer carries end of stream, or an empty one follows.
std::vector<Out> chunks(int count, int frames, int64_t firstFrame, bool emptyEos) {
    std::vector<Out> outs;
    for (int c = 0; c < count; ++c) {
        Out o;
        const int64_t first = firstFrame + static_cast<int64_t>(c) * frames;
        o.ptsUs = first * 1'000'000 / 44100;
        for (int f = 0; f < frames; ++f) {
            const auto v = static_cast<int16_t>((first + f) % 30000);
            for (int ch = 0; ch < 2; ++ch) {
                o.bytes.push_back(static_cast<uint8_t>(v & 0xFF));
                o.bytes.push_back(static_cast<uint8_t>((static_cast<uint16_t>(v) >> 8) & 0xFF));
            }
        }
        o.eos = !emptyEos && c == count - 1;
        outs.push_back(o);
        if (c % 2 == 1) outs.push_back(Out{false, {}, 0, false});
    }
    if (emptyEos) outs.push_back(Out{true, {}, 0, true});
    return outs;
}

struct Heard {
    std::vector<float> samples;
    int hears = 0;
};

void sink(void* context, const float* interleaved, int frames, int channels, int) {
    auto* h = static_cast<Heard*>(context);
    h->samples.insert(h->samples.end(), interleaved, interleaved + static_cast<size_t>(frames) * channels);
}

class Listener final : public TrackListener {
public:
    bool hear(TrackFeed& feed) override {
        ++heard.hears;
        return feed.deliver(sink, &heard);
    }
    Heard heard;
};

// The order the export's decode followed, written out: a step pushes the next ready buffer's samples and ends the
// track after the one flagged end of stream; deliver steps until the rate is known, aims, then hears and steps.
struct Written {
    Written(int64_t startMs, const std::vector<Out>& outs) : feed(startMs), outs(outs) {}
    void step() {
        if (next >= outs.size()) return;  // every list here ends on an end-of-stream buffer first
        const Out& o = outs[next++];
        if (!o.ready) return;
        if (!o.bytes.empty()) feed.pushDecoded(o.bytes.data(), o.bytes.size(), false, 2, 44100, o.ptsUs);
        if (o.eos) ended = true;
    }
    template <typename Aim>
    bool deliver(Aim aim, Heard& h) {
        while (!feed.hasRate() && !ended) step();
        if (!feed.hasRate()) return false;
        aim(feed);
        while (!feed.deliver(sink, &h)) {
            if (ended) return false;
            step();
        }
        return true;
    }
    TrackFeed feed;
    const std::vector<Out>& outs;
    size_t next = 0;
    bool ended = false;
};

void rules() {
    const ExtractorSpan whole = extractorSpan(TrackSpan{3, 4096, -1});
    check(whole.offset == 0 && whole.length == 0x7ffffffffffffffLL, "no declared span: the whole file, as MediaExtractor's");
    const ExtractorSpan declared = extractorSpan(TrackSpan{3, 4096, 77000});
    check(declared.offset == 4096 && declared.length == 77000, "a declared span is passed as declared");
    check(decodeSeekUs(0) == -1 && decodeSeekUs(-5) == -1, "a range from the start does not seek");
    check(decodeSeekUs(1741) == 1'741'000, "a later range seeks to its start in us");
    check(isFloatPcm(true, 4), "pcm-encoding 4 (ENCODING_PCM_FLOAT) is float");
    check(!isFloatPcm(false, 4) && !isFloatPcm(true, 2) && !isFloatPcm(true, 3), "no key, 16-bit or 8-bit is not float");
    check(isAudioMime("audio/mp4a-latm") && isAudioMime("audio/flac"), "audio mime types");
    check(!isAudioMime("video/avc") && !isAudioMime("audi") && !isAudioMime(nullptr), "other mime types");
}

void decodeRule() {
    for (bool emptyEos : {false, true}) {
        PortLog log;
        DecodeSource source(std::make_unique<FakePort>(chunks(9, 1024, 0, emptyEos), 9, &log));
        TrackFeed feed(0);
        int steps = 0;
        while (source.step(feed) && steps < 1000) ++steps;
        check(steps < 1000, "the decode ends");
        check(!source.step(feed), "an ended decode stays ended");
        check(log.inputsAfterEos == 0, "no input after end of stream was queued");
        check(log.taken == log.released && !log.releasedUntaken, "each output buffer is handed back once");
        check(log.taken == 9 + (emptyEos ? 1 : 0), "every buffer up to the end-of-stream one is taken");
    }
}

void pumpOrder() {
    // A range from 1000 ms: the decoder lands on the 1024-frame sync chunk holding it, frame 44032.
    const int64_t startMs = 1000;
    const int64_t sync = (startMs * 44100 / 1000) / 1024 * 1024;
    for (int fps : {24, 30, 60}) {
        for (bool emptyEos : {false, true}) {
            const auto outs = chunks(40, 1024, sync, emptyEos);
            PortLog log;
            TrackPump pump(startMs, std::make_unique<DecodeSource>(std::make_unique<FakePort>(outs, 40, &log)));
            Written written(startMs, outs);
            Listener listener;
            Heard reference;
            bool same = true;
            for (int64_t n = 0; n < 100; ++n) {
                const bool a = pump.deliverFrame(n, fps, listener);
                const bool b = written.deliver([&](TrackFeed& f) { f.setTargetFrame(n, fps); }, reference);
                same = same && a == b;
            }
            check(same, "each frame's result is the written order's at " + std::to_string(fps) + " fps");
            check(listener.heard.samples == reference.samples, "the same samples at " + std::to_string(fps) + " fps");
            check(pump.ended(), "the track ran out within 100 frames at " + std::to_string(fps) + " fps");
            const int64_t first = startMs * 44100 / 1000;
            check(!reference.samples.empty() &&
                      reference.samples[0] == static_cast<float>(static_cast<int16_t>(first % 30000)) / 32768.0f,
                  "the range opens on its first frame, the sync chunk's lead cut");
        }
    }
    // A millisecond target: the pre-roll's.
    const auto outs = chunks(12, 1024, 0, false);
    PortLog log;
    TrackPump pump(0, std::make_unique<DecodeSource>(std::make_unique<FakePort>(outs, 12, &log)));
    Written written(0, outs);
    Listener listener;
    Heard reference;
    const bool a = pump.deliverMs(93, listener);
    const bool b = written.deliver([](TrackFeed& f) { f.setTargetMs(93); }, reference);
    check(a && b && listener.heard.samples == reference.samples, "a millisecond target is the written order's");
    check(listener.heard.samples.size() == static_cast<size_t>(93 * 44100 / 1000) * 2, "93 ms is 4101 frames");
    check(!pump.deliverMs(5000, listener), "a target past the track's end is false");
    // A decoder that ends before telling a rate.
    PortLog empty;
    TrackPump silent(0, std::make_unique<DecodeSource>(std::make_unique<FakePort>(std::vector<Out>{Out{true, {}, 0, true}}, 0, &empty)));
    check(!silent.deliverFrame(0, 30, listener) && silent.ended(), "no rate: nothing heard, the track ended");
}

// ── AIFF over a descriptor ──────────────────────────────────────────────────────────────────────────────────

void be16(std::vector<uint8_t>& b, int v) {
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void be32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// A mono 16-bit AIFF at `rate`: a ramp, frame f holding f % 30000.
std::vector<uint8_t> aiff(uint32_t rate, uint32_t frames) {
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

// A temporary file holding `bytes`, unlinked at once; its descriptor.
int tempFile(const std::vector<uint8_t>& bytes) {
    char path[] = "/tmp/track_pump_testXXXXXX";
    const int fd = ::mkstemp(path);
    if (fd < 0) return -1;
    ::unlink(path);
    size_t at = 0;
    while (at < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + at, bytes.size() - at);
        if (n <= 0) break;
        at += static_cast<size_t>(n);
    }
    return fd;
}

float rampAt(int64_t frame) { return static_cast<float>(static_cast<int16_t>(frame % 30000)) / 32768.0f; }

void aiffOpen() {
    const std::vector<uint8_t> file = aiff(8000, 8000 * 3);
    const int fd = tempFile(file);
    check(fd >= 0, "temp file");
    if (fd < 0) return;
    int decoderOpens = 0;
    const DecoderOpener never = [&](const TrackSpan&, int64_t) -> std::unique_ptr<DecodePort> {
        ++decoderOpens;
        return nullptr;
    };
    // Twice over the same descriptor: the second open reads from the start again.
    for (int round = 0; round < 2; ++round) {
        auto source = openTrack(TrackSpan{fd, 0, -1}, 1741, never);
        check(source != nullptr, "an AIFF opens through its own reader, round " + std::to_string(round));
        if (!source) continue;
        TrackPump pump(1741, std::move(source));
        Listener listener;
        check(pump.deliverFrame(0, 30, listener), "frame 0 is heard");
        // llround(1 * 8000 / 30) = 267 frames from 1741 ms (frame 13928).
        check(listener.heard.samples.size() == 267 && listener.heard.samples[0] == rampAt(13928) &&
                  listener.heard.samples[266] == rampAt(13928 + 266),
              "the AIFF lead-in is cut at the range start, round " + std::to_string(round));
    }
    check(decoderOpens == 0, "an AIFF never reaches the platform decoder");
    check(::fcntl(fd, F_GETFD) != -1, "the span's descriptor stays open: each source reads a duplicate");

    // A declared span inside a larger descriptor (an asset's offset and length).
    std::vector<uint8_t> padded(512 + file.size() + 64, 0xAB);
    std::copy(file.begin(), file.end(), padded.begin() + 512);
    std::fill(padded.end() - 64, padded.end(), static_cast<uint8_t>(0xCD));
    const int fd2 = tempFile(padded);
    auto inside = openTrack(TrackSpan{fd2, 512, static_cast<int64_t>(file.size())}, 0, never);
    check(inside != nullptr, "an AIFF inside a declared span opens");
    if (inside) {
        TrackPump pump(0, std::move(inside));
        Listener listener;
        check(pump.deliverMs(77, listener) && listener.heard.samples.size() == 616 &&
                  listener.heard.samples[615] == rampAt(615),
              "the declared span reads from its offset (77 ms at 8 kHz is 616 frames)");
    }

    // Not an AIFF: the platform decoder is asked, with the span and the start.
    const int fd3 = tempFile(std::vector<uint8_t>(4096, 0x5A));
    TrackSpan seen;
    int64_t seenStart = -1;
    const DecoderOpener record = [&](const TrackSpan& span, int64_t startMs) -> std::unique_ptr<DecodePort> {
        seen = span;
        seenStart = startMs;
        return nullptr;
    };
    check(openTrack(TrackSpan{fd3, 0, -1}, 174, record) == nullptr, "no decoder: no track");
    check(seen.fd == fd3 && seen.length == -1 && seenStart == 174, "the decoder gets the span and the start");
    PortLog log;
    const DecoderOpener fake = [&](const TrackSpan&, int64_t) -> std::unique_ptr<DecodePort> {
        return std::make_unique<FakePort>(chunks(2, 1024, 0, false), 2, &log);
    };
    check(openTrack(TrackSpan{fd3, 0, -1}, 0, fake) != nullptr, "a decoder that opens is the track");
    ::close(fd);
    ::close(fd2);
    ::close(fd3);
}

}  // namespace

int main() {
    rules();
    decodeRule();
    pumpOrder();
    aiffOpen();
    if (failures) {
        std::printf("%d track pump check(s) failed\n", failures);
        return 1;
    }
    std::printf("track pump: all checks passed\n");
    return 0;
}
