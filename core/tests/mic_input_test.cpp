// Host test for core/audio/ingest/MicInput: live input over a fake device stream. The formats asked for in
// capturePlan's order with the source preset handed in, a refused start ending the try as a refused
// startRecording did, the callback's blocks reaching the ring with their rate (float and 16-bit), nothing
// written after stop, a disconnect reopening within the budget, any other error ending the input, and a late
// report from a replaced stream changing nothing.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore core/tests/mic_input_test.cpp
//   core/audio/ingest/MicInput.cpp core/audio/ingest/CapturePlan.cpp core/audio/ingest/PcmRing.cpp
//   -o mic_input_test && ./mic_input_test
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "audio/ingest/MicInput.hpp"

using geode::audio::ingest::Cursor;
using geode::audio::ingest::MicInput;
using geode::audio::ingest::MicRequest;
using geode::audio::ingest::MicStream;
using geode::audio::ingest::MicStreams;
using geode::audio::ingest::PcmRing;
using geode::audio::ingest::ReopenBudget;

static int failures = 0;
#define CHECK(cond, ...)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            std::fprintf(stderr, "FAIL %s:%d ", __FILE__, __LINE__); \
            std::fprintf(stderr, __VA_ARGS__);                       \
            std::fprintf(stderr, "\n");                              \
            ++failures;                                              \
        }                                                            \
    } while (0)

namespace {

struct FakeStreams;

struct FakeStream final : MicStream {
    FakeStream(FakeStreams& owner, bool startOk) : owner(owner), startOk(startOk) {}
    ~FakeStream() override;
    bool start() override {
        started = startOk;
        return startOk;
    }
    const void* id() const override { return this; }

    FakeStreams& owner;
    bool startOk;
    bool started = false;
};

struct FakeStreams final : MicStreams {
    std::function<bool(const MicRequest&)> accepts = [](const MicRequest&) { return true; };
    bool startOk = true;
    std::vector<MicRequest> asked;
    FakeStream* live = nullptr;
    int closed = 0;

    std::unique_ptr<MicStream> open(const MicRequest& request, MicInput&) override {
        asked.push_back(request);
        if (!accepts(request)) return nullptr;
        auto stream = std::make_unique<FakeStream>(*this, startOk);
        live = stream.get();
        return stream;
    }
};

FakeStream::~FakeStream() {
    ++owner.closed;
    if (owner.live == this) owner.live = nullptr;
}

std::shared_ptr<PcmRing> micRing() { return std::make_shared<PcmRing>(1 << 15, 1); }

void testPlanOrderAndPreset() {
    FakeStreams streams;
    // The device gives 16-bit only, and not at its own rate: 44.1 kHz float, 44.1 kHz 16-bit, 48 kHz float,
    // 48 kHz 16-bit, as the mono ladder runs.
    streams.accepts = [](const MicRequest& r) { return r.format.rateHz == 48000 && !r.format.asFloat; };
    MicInput mic(streams);
    CHECK(mic.start(micRing(), 44100, true), "starts on the fourth format");
    CHECK(mic.active(), "active once started");
    CHECK(streams.asked.size() == 4, "%zu formats asked, want 4", streams.asked.size());
    const int rates[] = {44100, 44100, 48000, 48000};
    const bool floats[] = {true, false, true, false};
    for (size_t i = 0; i < streams.asked.size() && i < 4; ++i) {
        const MicRequest& r = streams.asked[i];
        CHECK(r.format.rateHz == rates[i] && r.format.asFloat == floats[i] && r.format.channels == 1,
              "format %zu: %d Hz x%d float %d", i, r.format.rateHz, r.format.channels, r.format.asFloat);
        CHECK(r.unprocessed, "format %zu keeps the UNPROCESSED source", i);
        CHECK(r.capacityFrames == MicInput::kCapacityFrames, "format %zu capacity %d", i, r.capacityFrames);
    }
    CHECK(mic.start(micRing(), 44100, true) && streams.asked.size() == 4, "a second start opens nothing");
    mic.stop();

    FakeStreams unknown;
    MicInput other(unknown);
    CHECK(other.start(micRing(), 0, false), "an unknown device rate starts");
    CHECK(unknown.asked.size() == 1 && unknown.asked[0].format.rateHz == 48000 && unknown.asked[0].format.asFloat,
          "an unknown device rate asks 48 kHz float first");
    CHECK(!unknown.asked[0].unprocessed, "VOICE_RECOGNITION where UNPROCESSED is not supported");
}

void testRefusals() {
    FakeStreams none;
    none.accepts = [](const MicRequest&) { return false; };
    MicInput mic(none);
    CHECK(!mic.start(micRing(), 48000, false), "no format: no start");
    CHECK(!mic.active(), "inactive after a refused open");
    CHECK(none.asked.size() == 6, "%zu formats asked, want the whole mono ladder (6)", none.asked.size());
    CHECK(!mic.onAudio(nullptr, 0), "no ring is written after a refused open");

    FakeStreams refusing;
    refusing.startOk = false;
    MicInput other(refusing);
    CHECK(!other.start(micRing(), 48000, false), "a refused start fails");
    CHECK(refusing.asked.size() == 1, "a refused start tries no other format (%zu)", refusing.asked.size());
    CHECK(refusing.closed == 1 && !other.active(), "the refused stream is closed");
    CHECK(!other.start(nullptr, 48000, false), "no ring: no start");
}

void testWrites() {
    FakeStreams streams;
    streams.accepts = [](const MicRequest& r) { return r.format.asFloat; };
    auto ring = micRing();
    Cursor reader(*ring, 4096);
    MicInput mic(streams);
    CHECK(mic.start(ring, 44100, false), "float stream starts");
    const float block[] = {0.25f, -0.5f, 0.75f};
    CHECK(mic.onAudio(block, 3), "the callback continues while running");
    Cursor::Take take = reader.take();
    CHECK(take.frames == 3 && take.sampleRateHz == 44100, "%d frames at %d Hz", take.frames, take.sampleRateHz);
    for (int i = 0; i < take.frames && i < 3; ++i) {
        CHECK(reader.interleaved()[i] == block[i], "sample %d %f", i, reader.interleaved()[i]);
    }
    mic.stop();
    CHECK(!mic.active() && streams.live == nullptr, "stop closes the stream");
    CHECK(!mic.onAudio(block, 3), "the callback stops after stop");
    CHECK(reader.take().frames == 0, "nothing written after stop");

    FakeStreams shorts;
    shorts.accepts = [](const MicRequest& r) { return !r.format.asFloat; };
    MicInput mic16(shorts);
    CHECK(mic16.start(ring, 48000, false), "16-bit stream starts");
    const int16_t pcm[] = {16384, -32768};
    CHECK(mic16.onAudio(pcm, 2), "16-bit block taken");
    take = reader.take();
    CHECK(take.frames == 2 && take.sampleRateHz == 48000, "%d frames at %d Hz", take.frames, take.sampleRateHz);
    CHECK(take.frames == 2 && reader.interleaved()[0] == 0.5f && reader.interleaved()[1] == -1.0f,
          "16-bit scaled by 1/32768");
}

void testErrors() {
    FakeStreams streams;
    MicInput mic(streams);
    CHECK(mic.start(micRing(), 48000, false), "starts");
    FakeStream* first = streams.live;
    mic.onClosed(first, true, 0);
    CHECK(mic.active() && streams.live && streams.live != first, "a disconnect opens a new stream");
    CHECK(streams.live && streams.live->started, "and starts it");
    CHECK(streams.asked.size() == 2 && streams.asked[1].format.rateHz == 48000, "the ladder again from the top");
    mic.onClosed(first, true, 0);
    CHECK(streams.asked.size() == 2, "a late report from a replaced stream changes nothing");

    mic.onClosed(streams.live, true, 1000);
    mic.onClosed(streams.live, true, 2000);
    CHECK(mic.active() && streams.asked.size() == 4, "three reopens in the window");
    mic.onClosed(streams.live, true, 3000);
    CHECK(!mic.active() && streams.asked.size() == 4, "a fourth in the window ends the input");
    CHECK(!mic.onAudio(nullptr, 0), "an ended input writes no more");

    CHECK(mic.start(micRing(), 48000, false), "a new start after the end");
    mic.onClosed(streams.live, false, 3000);
    CHECK(!mic.active(), "an error other than a disconnect ends the input");

    FakeStreams flaky;
    MicInput other(flaky);
    CHECK(other.start(micRing(), 48000, false), "starts");
    flaky.accepts = [](const MicRequest&) { return false; };
    other.onClosed(flaky.live, true, 0);
    CHECK(!other.active(), "a disconnect the device will not reopen ends the input");
}

void testBudget() {
    ReopenBudget budget;
    CHECK(budget.take(0) && budget.take(1) && budget.take(2), "three at once");
    CHECK(!budget.take(3), "a fourth within the window is refused");
    CHECK(!budget.take(ReopenBudget::kWindowMs - 1), "still inside the window of the first");
    CHECK(budget.take(ReopenBudget::kWindowMs), "the first has left the window");
    budget.reset();
    CHECK(budget.take(4) && budget.take(5) && budget.take(6), "a reset forgets every reopen");
}

}  // namespace

int main() {
    testPlanOrderAndPreset();
    testRefusals();
    testWrites();
    testErrors();
    testBudget();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("mic_input_test: ok\n");
    return 0;
}
