// Host test for core/audio/output/OutputFifo: the playback output's FIFO and its position.
//
// What the output must keep true whatever the device does: PCM written and read in odd chunk sizes
// arrives every frame once, in order; a dry consumer gets silence and is counted once per dry spell;
// the position the sink reads never goes back, stands still in pause, starts again after a flush,
// equals the frames heard at 44.1, 48 and 96 kHz and reaches the last frame after stop(); a volume
// change ramps with no step above the ramp's slope; and a producer thread and a consumer thread
// exchange a self-describing pattern intact.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore core/tests/output_fifo_test.cpp
//   core/audio/output/OutputFifo.cpp -o output_fifo_test && ./output_fifo_test
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "audio/output/OutputFifo.hpp"

using geode::audio::output::OutputFifo;
using geode::audio::output::StreamTime;

static int failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            std::fprintf(stderr, "FAIL %s:%d ", __FILE__, __LINE__); \
            std::fprintf(stderr, __VA_ARGS__);              \
            std::fprintf(stderr, "\n");                     \
            ++failures;                                     \
        }                                                   \
    } while (0)

namespace {

constexpr int64_t kSecondNs = 1'000'000'000;

// Frame f of a stereo test signal: left f + 1, right -(f + 1). Never 0, so silence stands out, and
// exact in a float up to 2^24.
std::vector<float> stereo(int from, int frames) {
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        out[static_cast<size_t>(i) * 2] = static_cast<float>(from + i + 1);
        out[static_cast<size_t>(i) * 2 + 1] = -static_cast<float>(from + i + 1);
    }
    return out;
}

// A small deterministic generator, so a failure repeats.
struct Lcg {
    uint32_t state;
    int next(int bound) {
        state = state * 1664525u + 1013904223u;
        return static_cast<int>((state >> 8) % static_cast<uint32_t>(bound));
    }
};

// Checks `frames` stereo frames in `got` continue the test signal at `expected`; returns the frames
// that were signal (the rest, at the end, must be silence).
int checkStereo(const std::vector<float>& got, int frames, int expected) {
    int signal = 0;
    bool silence = false;
    for (int i = 0; i < frames; ++i) {
        const float left = got[static_cast<size_t>(i) * 2];
        const float right = got[static_cast<size_t>(i) * 2 + 1];
        if (left == 0.0f && right == 0.0f) {
            silence = true;
            continue;
        }
        CHECK(!silence, "frame %d: signal after silence in one read", i);
        CHECK(left == static_cast<float>(expected + signal + 1), "frame %d: left %f, expected %d", i,
              static_cast<double>(left), expected + signal + 1);
        CHECK(right == -left, "frame %d: right %f for left %f", i, static_cast<double>(right),
              static_cast<double>(left));
        ++signal;
    }
    return signal;
}

void testFormats() {
    CHECK(OutputFifo::supports(2, 2, 44100), "16-bit stereo 44.1k");
    CHECK(OutputFifo::supports(4, 1, 96000), "float mono 96k");
    CHECK(OutputFifo::supports(2, 2, 192000), "192k");
    CHECK(!OutputFifo::supports(3, 2, 48000), "8-bit is not taken");
    CHECK(!OutputFifo::supports(2, 6, 48000), "5.1 is not taken");
    CHECK(!OutputFifo::supports(2, 0, 48000), "no channels");
    CHECK(!OutputFifo::supports(2, 2, 4000), "4k is below the range");
    CHECK(!OutputFifo::supports(2, 2, 384000), "384k is above the range");
    CHECK(OutputFifo::bytesPerFrame(2, 2) == 4 && OutputFifo::bytesPerFrame(4, 2) == 8, "frame sizes");
    CHECK(OutputFifo::bytesPerFrame(4, 1) == 4 && OutputFifo::bytesPerFrame(21, 2) == 0, "frame sizes");

    // 250 ms of 16-bit stereo at 48 kHz is 48000 bytes: kept as asked.
    CHECK(OutputFifo::capacityFrames(48000, 2, 2, 48000) == 12000, "the sink's buffer is the capacity");
    CHECK(OutputFifo::capacityFrames(48000, 2, 2, 0) == 12000, "no size: 250 ms");
    CHECK(OutputFifo::capacityFrames(48000, 2, 2, 400) == 4800, "never under 100 ms");
    CHECK(OutputFifo::capacityFrames(48000, 2, 2, 4'000'000) == 48000, "never over a second");
    CHECK(OutputFifo::capacityFrames(44100, 1, 4, 44100) == 11025, "float mono");
    CHECK(OutputFifo::capacityFrames(48000, 6, 2, 48000) == 0, "an unsupported format has no capacity");
}

void testOddChunksLoseAndRepeatNothing() {
    OutputFifo fifo(48000, 2, 1000);
    const int total = 50000;
    const std::vector<float> signal = stereo(0, total);
    std::vector<float> got(static_cast<size_t>(1024) * 2);
    Lcg rng{7u};
    int written = 0;
    int heard = 0;
    int offered = 0;
    while (heard < total) {
        if (written < total) {
            const int chunk = std::min(1 + rng.next(700), total - written);
            const int taken = fifo.writeFloat(signal.data() + static_cast<size_t>(written) * 2, chunk);
            CHECK(taken >= 0 && taken <= chunk, "took %d of %d", taken, chunk);
            CHECK(fifo.pendingFrames() <= fifo.capacityFrames(), "holds %lld of %d",
                  static_cast<long long>(fifo.pendingFrames()), fifo.capacityFrames());
            written += taken;
            ++offered;
        }
        const int burst = 1 + rng.next(511);
        fifo.read(got.data(), burst);
        heard += checkStereo(got, burst, heard);
    }
    CHECK(heard == total, "heard %d of %d", heard, total);
    CHECK(fifo.framesWritten() == total, "written %lld", static_cast<long long>(fifo.framesWritten()));
    CHECK(fifo.framesPlayed() == total, "played %lld", static_cast<long long>(fifo.framesPlayed()));
    CHECK(offered > 0, "nothing was offered");
}

void testFullFifoTakesWhatFits() {
    OutputFifo fifo(48000, 2, 1000);
    const std::vector<float> signal = stereo(0, 2000);
    CHECK(fifo.writeFloat(signal.data(), 800) == 800, "800 fit");
    CHECK(fifo.writeFloat(signal.data() + 1600, 800) == 200, "only 200 more fit");
    CHECK(fifo.writeFloat(signal.data() + 2000, 800) == 0, "a full fifo takes nothing");
    CHECK(fifo.writeFloat(nullptr, 10) == 0 && fifo.writeFloat(signal.data(), 0) == 0, "refused blocks");
    std::vector<float> got(static_cast<size_t>(1000) * 2);
    fifo.read(got.data(), 1000);
    CHECK(checkStereo(got, 1000, 0) == 1000, "the 1000 frames taken, in order");
}

void testEncodings() {
    OutputFifo fifo(44100, 2, 4410);
    const int16_t pcm[] = {0, 16384, -32768, 32767, 1, -1};
    CHECK(fifo.writeInt16(pcm, 3) == 3, "three 16-bit frames");
    std::vector<float> got(6);
    fifo.read(got.data(), 3);
    CHECK(got[0] == 0.0f && got[1] == 0.5f && got[2] == -1.0f, "x / 32768");
    CHECK(got[3] == 32767.0f / 32768.0f && got[4] == 1.0f / 32768.0f && got[5] == -1.0f / 32768.0f, "x / 32768");

    // Raw bytes at an odd address, with a trailing part of a frame.
    const float floats[] = {0.25f, -0.75f, 1.5f, -2.0f};
    std::vector<unsigned char> bytes(1 + sizeof(floats) + 3);
    std::memcpy(bytes.data() + 1, pcm, sizeof(pcm));
    CHECK(fifo.writeBytes(bytes.data() + 1, sizeof(pcm) + 3, 2) == 3, "whole frames only");
    fifo.read(got.data(), 3);
    CHECK(got[1] == 0.5f && got[2] == -1.0f, "16-bit bytes");

    std::memcpy(bytes.data() + 1, floats, sizeof(floats));
    CHECK(fifo.writeBytes(bytes.data() + 1, sizeof(floats), 4) == 2, "two float frames");
    fifo.read(got.data(), 2);
    CHECK(got[0] == 0.25f && got[1] == -0.75f && got[2] == 1.5f && got[3] == -2.0f, "float passes as it is");

    CHECK(fifo.writeBytes(bytes.data(), 16, 3) == 0, "8-bit is refused");
    CHECK(fifo.framesWritten() == 8, "written %lld", static_cast<long long>(fifo.framesWritten()));
}

void testUnderrunIsSilenceAndCountedOnce() {
    OutputFifo fifo(48000, 2, 4800);
    std::vector<float> got(static_cast<size_t>(256) * 2, 9.0f);

    // Before the first frame: silence, not an underrun.
    fifo.read(got.data(), 256);
    CHECK(checkStereo(got, 256, 0) == 0, "silence before the first frame");
    CHECK(fifo.underruns() == 0, "a dry start is not an underrun");
    CHECK(fifo.takeEvents() == 0, "no events yet");

    const std::vector<float> signal = stereo(0, 1000);
    CHECK(fifo.writeFloat(signal.data(), 100) == 100, "wrote 100");
    std::fill(got.begin(), got.end(), 9.0f);
    fifo.read(got.data(), 256);
    CHECK(checkStereo(got, 256, 0) == 100, "100 frames, then silence");
    CHECK(got[static_cast<size_t>(100) * 2] == 0.0f && got[static_cast<size_t>(255) * 2 + 1] == 0.0f, "zero-filled");
    CHECK(fifo.underruns() == 1, "underruns %lld", static_cast<long long>(fifo.underruns()));

    // The same dry spell is one underrun however many bursts it lasts.
    fifo.read(got.data(), 256);
    fifo.read(got.data(), 256);
    CHECK(fifo.underruns() == 1, "one dry spell, one underrun");
    CHECK(fifo.takeEvents() == geode::audio::output::kEventUnderrun, "the underrun is reported");
    CHECK(fifo.takeEvents() == 0, "and only once");

    // Served whole again, then dry again: a second one.
    CHECK(fifo.writeFloat(signal.data() + 200, 300) == 300, "wrote 300");
    fifo.read(got.data(), 256);
    CHECK(checkStereo(got, 256, 100) == 256, "a whole burst");
    fifo.read(got.data(), 256);
    CHECK(checkStereo(got, 256, 356) == 44, "the rest");
    CHECK(fifo.underruns() == 2, "underruns %lld", static_cast<long long>(fifo.underruns()));

    // After stop() the fifo is meant to run dry.
    CHECK(fifo.writeFloat(signal.data() + 800, 300) == 300, "wrote 300");
    fifo.read(got.data(), 256);
    fifo.stop(0);
    fifo.read(got.data(), 256);
    fifo.read(got.data(), 256);
    CHECK(fifo.underruns() == 2, "playing out after stop is not an underrun");
    CHECK(fifo.pendingFrames() == 0, "played out");
}

void testFlush() {
    OutputFifo fifo(48000, 2, 1000);
    const std::vector<float> signal = stereo(0, 3000);
    std::vector<float> got(static_cast<size_t>(1000) * 2);
    CHECK(fifo.writeFloat(signal.data(), 1000) == 1000, "filled");
    fifo.read(got.data(), 300);
    CHECK(fifo.framesPlayed() == 300 && fifo.pendingFrames() == 700, "300 played");

    fifo.flush();
    CHECK(fifo.framesWritten() == 0 && fifo.framesPlayed() == 0 && fifo.pendingFrames() == 0, "a flush empties it");
    StreamTime none;
    CHECK(fifo.positionUs(0, none, 0) == 0, "and starts the position again");

    // The whole capacity is free at once, before the consumer has run again.
    CHECK(fifo.writeFloat(signal.data() + 4000, 1000) == 1000, "room for a full buffer after a flush");
    fifo.read(got.data(), 1000);
    CHECK(checkStereo(got, 1000, 2000) == 1000, "only what was written after the flush is played");
    CHECK(fifo.framesPlayed() == 1000, "played %lld", static_cast<long long>(fifo.framesPlayed()));

    // A flush of an empty fifo, twice, changes nothing.
    fifo.flush();
    fifo.flush();
    fifo.read(got.data(), 100);
    CHECK(checkStereo(got, 100, 0) == 0, "silence");
    CHECK(fifo.underruns() == 0, "a flush is not an underrun");
}

// A device stream as Oboe shows one: it pulls bursts on its own clock, and says which of its frames
// (silence included) is at the speaker now, `latency` frames behind what it has pulled.
struct Device {
    OutputFifo& fifo;
    int rate;
    int latency;
    int64_t pulled = 0;   // frames pulled from this stream
    int64_t clockNs = 0;  // advances with every burst, and with idle()
    std::vector<float> buffer;

    Device(OutputFifo& output, int sampleRateHz, int latencyFrames)
        : fifo(output), rate(sampleRateHz), latency(latencyFrames), buffer(4096) {}

    void burst(int frames) {
        fifo.read(buffer.data(), frames);
        pulled += frames;
        clockNs += static_cast<int64_t>(frames) * kSecondNs / rate;
    }
    void idle(int64_t ns) { clockNs += ns; }
    StreamTime time() const {
        StreamTime t;
        t.valid = pulled >= latency;
        t.framePosition = pulled - latency;
        t.timeNs = clockNs;
        return t;
    }
};

// Tops the fifo up with the test signal from `next` on; returns the new `next`.
int topUp(OutputFifo& fifo, const std::vector<float>& signal, int next, int total) {
    if (next >= total) return next;
    return next + fifo.writeFloat(signal.data() + static_cast<size_t>(next) * 2, total - next);
}

void testPositionAt(int rate) {
    const int latency = rate / 20;  // 50 ms between the callback and the speaker
    const int burst = 192;
    const int total = rate * 4;
    const std::vector<float> signal = stereo(0, total);
    OutputFifo fifo(rate, 2, rate / 4);
    Device device(fifo, rate, latency);
    int next = 0;
    int64_t last = 0;
    const auto heard = [&](int64_t played) { return std::max<int64_t>(played - latency, 0); };
    const auto step = [&](const char* phase) {
        const int64_t us = fifo.positionUs(device.clockNs, device.time(), latency);
        CHECK(us >= last, "%d Hz %s: position went back, %lld after %lld", rate, phase, static_cast<long long>(us),
              static_cast<long long>(last));
        last = us;
        return us;
    };

    // A dry start: the stream pulls silence before the sink has written anything.
    fifo.play(device.clockNs);
    for (int i = 0; i < 5; ++i) device.burst(burst);
    CHECK(step("dry start") == 0, "%d Hz: nothing written, nothing heard", rate);

    // One second of playing: the position is the frames heard, to the frame.
    for (int i = 0; i < rate / burst; ++i) {
        next = topUp(fifo, signal, next, total);
        device.burst(burst);
        const int64_t us = step("playing");
        const int64_t expected = heard(fifo.framesPlayed()) * 1'000'000 / rate;
        CHECK(us == expected, "%d Hz: position %lld us, heard %lld us", rate, static_cast<long long>(us),
              static_cast<long long>(expected));
    }
    CHECK(fifo.underruns() == 0, "%d Hz: no underrun while topped up", rate);
    CHECK(last > 800'000, "%d Hz: a second played, position %lld us", rate, static_cast<long long>(last));

    // Without a timestamp the frames read less the stream's latency say the same.
    StreamTime none;
    CHECK(fifo.positionUs(device.clockNs, none, latency) == last, "%d Hz: the estimate agrees", rate);

    // Pause: it stands still, whatever the stream or the clock say.
    const int64_t atPause = step("pause");
    fifo.pause();
    device.idle(kSecondNs);
    CHECK(step("paused") == atPause, "%d Hz: paused, the position stands", rate);
    StreamTime lying = device.time();
    lying.framePosition += rate;
    CHECK(fifo.positionUs(device.clockNs, lying, 0) == atPause, "%d Hz: paused, a moving timestamp is ignored", rate);

    // Resume: a timestamp from before the pause is not run on from.
    StreamTime stale = device.time();
    stale.timeNs -= kSecondNs / 2;
    fifo.play(device.clockNs);
    CHECK(fifo.positionUs(device.clockNs, stale, latency) == atPause, "%d Hz: a stale timestamp moves nothing", rate);
    for (int i = 0; i < rate / 2 / burst; ++i) {
        next = topUp(fifo, signal, next, total);
        device.burst(burst);
        const int64_t us = step("resumed");
        CHECK(us == heard(fifo.framesPlayed()) * 1'000'000 / rate, "%d Hz: resumed, position %lld us", rate,
              static_cast<long long>(us));
    }
    CHECK(last > atPause, "%d Hz: it moves again after resume", rate);
    CHECK((fifo.takeEvents() & geode::audio::output::kEventAdvancing) != 0, "%d Hz: advancing was reported", rate);

    // A new device stream (a route change): its frame count starts at 0, the position carries on.
    fifo.streamRestarted();
    device.pulled = 0;
    for (int i = 0; i < rate / 2 / burst; ++i) {
        next = topUp(fifo, signal, next, total);
        device.burst(burst);
        const int64_t us = step("new stream");
        CHECK(us == heard(fifo.framesPlayed()) * 1'000'000 / rate, "%d Hz: new stream, position %lld us", rate,
              static_cast<long long>(us));
    }

    // Flush (a seek): back to 0, then the frames heard of what was written after it.
    fifo.flush();
    last = 0;
    CHECK(step("flushed") == 0, "%d Hz: a flush starts the position again", rate);
    const int afterFlush = next;
    for (int i = 0; i < rate / 2 / burst; ++i) {
        next = topUp(fifo, signal, next, total);
        device.burst(burst);
        const int64_t us = step("after flush");
        CHECK(us == heard(fifo.framesPlayed()) * 1'000'000 / rate, "%d Hz: after flush, position %lld us", rate,
              static_cast<long long>(us));
    }
    CHECK(last > 0, "%d Hz: it moves after a flush", rate);

    // Stop: what is waiting plays out, and the position ends on the last frame written, exactly.
    next = topUp(fifo, signal, next, std::min(total, next + 777));
    const int64_t written = next - afterFlush;
    CHECK(fifo.framesWritten() == written, "%d Hz: written %lld", rate, static_cast<long long>(fifo.framesWritten()));
    fifo.stop(device.clockNs);
    CHECK(!fifo.ended(), "%d Hz: not ended while frames wait", rate);
    const int64_t underruns = fifo.underruns();
    for (int i = 0; i < rate / burst; ++i) {
        device.burst(burst);
        step("playing out");
    }
    CHECK(fifo.framesPlayed() == written, "%d Hz: everything played out", rate);
    CHECK(fifo.positionFrames() == written, "%d Hz: ended at %lld of %lld", rate,
          static_cast<long long>(fifo.positionFrames()), static_cast<long long>(written));
    CHECK(fifo.ended(), "%d Hz: ended", rate);
    CHECK(fifo.underruns() == underruns, "%d Hz: playing out is not an underrun", rate);
    // Media3 turns the position back into frames, rounding up, and is done when that reaches the frames written.
    const int64_t backToFrames = (last * rate + 999'999) / 1'000'000;
    CHECK(backToFrames == written, "%d Hz: %lld us is frame %lld, written %lld", rate, static_cast<long long>(last),
          static_cast<long long>(backToFrames), static_cast<long long>(written));
    device.burst(burst);
    CHECK(step("ended") == last, "%d Hz: it stays on the last frame", rate);
}

// The stream's timestamp as production has it (OboeOutput reads one every 200 ms and runs on from it by
// the clock in between, and the stream sometimes has none): aged, too old to run on from, from the
// future, and missing then back mid-play with an estimate that is off. Whatever it is given, the
// position never goes back, never passes the frames read, and stays within a buffer of the truth.
void testAgedFutureAndMissingTimestamps() {
    const int rate = 48000;
    const int latency = rate / 20;
    const int burst = 192;  // 4 ms: the device clock moves in whole nanoseconds
    const int total = rate * 4;
    const std::vector<float> signal = stereo(0, total);
    OutputFifo fifo(rate, 2, rate / 4);
    Device device(fifo, rate, latency);
    int next = 0;
    int64_t last = 0;
    const auto check = [&](const char* phase, const StreamTime& stamp, int64_t estimate, int64_t tolerance) {
        fifo.positionUs(device.clockNs, stamp, estimate);
        const int64_t frames = fifo.positionFrames();
        const int64_t played = fifo.framesPlayed();
        const int64_t truth = std::max<int64_t>(played - latency, 0);
        CHECK(frames >= last, "%s: position went back, %lld after %lld", phase, static_cast<long long>(frames),
              static_cast<long long>(last));
        CHECK(frames <= played, "%s: position %lld is ahead of the %lld frames read", phase,
              static_cast<long long>(frames), static_cast<long long>(played));
        CHECK(frames - truth <= tolerance && truth - frames <= tolerance, "%s: position %lld, truth %lld", phase,
              static_cast<long long>(frames), static_cast<long long>(truth));
        last = frames;
    };
    const auto pull = [&] {
        next = topUp(fifo, signal, next, total);
        device.burst(burst);
    };

    fifo.play(device.clockNs);
    for (int i = 0; i < 100; ++i) {
        pull();
        check("fresh", device.time(), latency, 2);
    }

    // Aged: read every 50 bursts (200 ms), run on from in between.
    StreamTime cached = device.time();
    for (int i = 0; i < 200; ++i) {
        if (i % 50 == 0) cached = device.time();
        pull();
        check("aged", cached, latency, 2);
    }

    // Never read again: past a second it is not run on from, and the estimate carries on from there.
    cached = device.time();
    for (int i = 0; i < 300; ++i) {
        pull();
        check("too old", cached, latency, 2);
    }

    // From the future, and far ahead: not believed.
    for (int i = 0; i < 50; ++i) {
        pull();
        StreamTime ahead = device.time();
        ahead.timeNs += kSecondNs / 20;
        ahead.framePosition += rate;
        check("future", ahead, latency, 2);
    }

    // Missing, then back, over and over, with an estimate that hears half the latency there is: the
    // position holds while the timestamp says less than the estimate did, and never jumps back.
    for (int i = 0; i < 200; ++i) {
        const bool missing = (i / 25) % 2 == 0;
        if (i % 25 == 0) cached = device.time();
        pull();
        check(missing ? "missing" : "back", missing ? StreamTime{} : cached, latency / 2, latency);
    }
    CHECK(fifo.underruns() == 0, "no underrun in the timestamp cases");
    CHECK(last > rate * 3, "the cases played on: %lld frames", static_cast<long long>(last));
}

// After stop() the end is reached by the clock even when the stream never reports or pulls again.
void testStopEndsWithoutAStream() {
    OutputFifo fifo(48000, 2, 12000);
    const std::vector<float> signal = stereo(0, 6000);
    StreamTime none;
    fifo.play(0);
    CHECK(fifo.writeFloat(signal.data(), 6000) == 6000, "wrote");
    fifo.stop(1000);
    CHECK(fifo.positionUs(1000, none, 0) == 0, "nothing heard yet");
    const int64_t halfway = fifo.positionUs(1000 + kSecondNs / 16, none, 0);  // 62.5 ms = 3000 frames
    CHECK(halfway == 62'500, "by the clock: %lld us", static_cast<long long>(halfway));
    CHECK(!fifo.ended(), "not yet ended");
    CHECK(fifo.positionUs(1000 + kSecondNs, none, 0) == 125'000, "never past the last frame written");
    CHECK(fifo.ended(), "ended by the clock");

    // Paused before stop: the clock does not run it on.
    OutputFifo held(48000, 2, 12000);
    held.play(0);
    CHECK(held.writeFloat(signal.data(), 6000) == 6000, "wrote");
    held.pause();
    held.stop(0);
    CHECK(held.positionUs(kSecondNs, none, 0) == 0 && !held.ended(), "paused: it waits");
    held.play(kSecondNs);
    CHECK(held.positionUs(kSecondNs + kSecondNs / 16, none, 0) == 62'500, "resumed: on from where it stood");
}

// After stop() the output plays out: the consumer learns when it has read the last frame (the stream
// then stops itself), and the pause the sink sends when the position reaches the end is ignored.
void testStopPlaysOutAndIgnoresPause() {
    OutputFifo fifo(48000, 2, 12000);
    const std::vector<float> signal = stereo(0, 1000);
    std::vector<float> got(static_cast<size_t>(600) * 2);
    StreamTime none;
    CHECK(!fifo.stopped() && !fifo.drained(), "an empty fifo that was not stopped is not drained");
    fifo.play(0);
    CHECK(fifo.writeFloat(signal.data(), 1000) == 1000, "wrote");
    fifo.read(got.data(), 600);
    CHECK(!fifo.drained(), "not stopped yet");
    fifo.stop(0);
    CHECK(fifo.stopped() && !fifo.drained(), "stopped, 400 frames still wait");

    // The pause is ignored: the position keeps moving to the end.
    fifo.pause();
    const int64_t early = fifo.positionUs(kSecondNs / 480, none, 0);
    CHECK(early > 0, "a pause after stop does not freeze the position: %lld us", static_cast<long long>(early));
    fifo.play(kSecondNs / 480);  // ignored too: nothing starts over
    fifo.read(got.data(), 300);
    CHECK(!fifo.drained(), "100 frames still wait");
    fifo.read(got.data(), 300);
    CHECK(checkStereo(got, 300, 900) == 100, "the last 100 frames, then silence");
    CHECK(fifo.drained(), "the consumer has read the last frame");
    CHECK(!fifo.ended(), "drained is not ended: the tail is still on its way to the speaker");
    CHECK(fifo.positionUs(kSecondNs, none, 0) == 1000 * 1'000'000LL / 48000, "the end is reached all the same");
    CHECK(fifo.ended(), "ended");
    CHECK(fifo.underruns() == 0, "playing out is not an underrun");

    // A pause before stop() is an ordinary pause, and stays one after it; play() then resumes.
    OutputFifo held(48000, 2, 12000);
    held.play(0);
    CHECK(held.writeFloat(signal.data(), 1000) == 1000, "wrote");
    held.read(got.data(), 600);
    held.positionUs(kSecondNs / 100, none, 0);
    held.pause();
    const int64_t frozen = held.positionUs(kSecondNs / 50, none, 0);
    held.stop(kSecondNs / 50);
    held.pause();
    CHECK(held.positionUs(kSecondNs, none, 0) == frozen, "paused before stop: the position stands");
    CHECK(!held.drained() && !held.ended(), "and nothing is drained");
    held.play(kSecondNs);
    held.read(got.data(), 600);
    CHECK(held.drained(), "resumed: it plays out");
    CHECK(held.positionUs(2 * kSecondNs, none, 0) == 1000 * 1'000'000LL / 48000 && held.ended(), "and ends");

    // A flush clears the stop.
    held.flush();
    CHECK(!held.stopped() && !held.drained(), "a flushed fifo is not stopped");
}

void testVolumeRamp() {
    const int rate = 48000;
    OutputFifo fifo(rate, 1, rate);
    const float slope = 1000.0f / (static_cast<float>(rate) * static_cast<float>(OutputFifo::kRampMs));
    const int rampFrames = rate * OutputFifo::kRampMs / 1000;
    std::vector<float> ones(static_cast<size_t>(rate), 1.0f);
    std::vector<float> got(4096);
    CHECK(fifo.writeFloat(ones.data(), rate) == rate, "a second of full scale");

    fifo.read(got.data(), 100);
    CHECK(got[0] == 1.0f && got[99] == 1.0f, "full volume passes the samples as they are");

    // Down to a quarter: no step above the slope, there within the ramp's share of kRampMs, then steady.
    fifo.setVolume(0.25f);
    fifo.read(got.data(), 1000);
    float previous = 1.0f;
    float largest = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        CHECK(got[static_cast<size_t>(i)] <= previous, "frame %d: the ramp down went up", i);
        largest = std::max(largest, previous - got[static_cast<size_t>(i)]);
        previous = got[static_cast<size_t>(i)];
    }
    CHECK(largest <= slope * 1.001f, "largest step %f, slope %f", static_cast<double>(largest), static_cast<double>(slope));
    CHECK(largest > 0.0f, "it ramped");
    CHECK(got[0] < 1.0f && got[0] > 0.99f, "the first frame is one step down, not a jump: %f", static_cast<double>(got[0]));
    const int reach = rampFrames * 3 / 4 + 2;
    CHECK(got[static_cast<size_t>(reach)] == 0.25f, "at the target after %d frames: %f", reach,
          static_cast<double>(got[static_cast<size_t>(reach)]));
    CHECK(got[999] == 0.25f, "and it stays there");
    CHECK(got[static_cast<size_t>(rampFrames / 4)] > 0.25f, "not there early");

    // Back up, and out of range values are clamped.
    fifo.setVolume(7.0f);
    fifo.read(got.data(), 1000);
    previous = 0.25f;
    largest = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        CHECK(got[static_cast<size_t>(i)] >= previous, "frame %d: the ramp up went down", i);
        largest = std::max(largest, got[static_cast<size_t>(i)] - previous);
        previous = got[static_cast<size_t>(i)];
    }
    CHECK(largest <= slope * 1.001f, "largest step up %f", static_cast<double>(largest));
    CHECK(got[999] == 1.0f, "clamped to full volume");
    fifo.setVolume(-3.0f);
    fifo.read(got.data(), 1000);
    CHECK(got[999] == 0.0f, "clamped to silence");
    fifo.setVolume(std::nanf(""));
    fifo.read(got.data(), 10);
    CHECK(got[9] == 0.0f, "a NaN is silence");

    // A volume set before anything was read is where the first frame starts: no fade-in of a muted cast.
    OutputFifo muted(rate, 1, rate);
    muted.setVolume(0.5f);
    CHECK(muted.writeFloat(ones.data(), 100) == 100, "wrote");
    muted.read(got.data(), 100);
    CHECK(got[0] == 0.5f && got[99] == 0.5f, "the first frame is at the volume set before it");
}

void testStall() {
    OutputFifo fifo(48000, 2, 12000);
    const std::vector<float> signal = stereo(0, 1000);
    std::vector<float> got(512);
    const int64_t t0 = 5 * kSecondNs;
    CHECK(!fifo.stalled(t0), "not playing");
    fifo.play(t0);
    CHECK(!fifo.stalled(t0), "nothing waiting");
    CHECK(fifo.writeFloat(signal.data(), 1000) == 1000, "wrote");
    CHECK(!fifo.stalled(t0), "the watch starts");
    CHECK(!fifo.stalled(t0 + OutputFifo::kStallNs - 1), "not yet");
    CHECK(fifo.stalled(t0 + OutputFifo::kStallNs), "playing, frames waiting, nobody asked");
    fifo.read(got.data(), 64);
    CHECK(!fifo.stalled(t0 + OutputFifo::kStallNs + 1), "the consumer asked: not stalled");
    fifo.pause();
    CHECK(!fifo.stalled(t0 + 10 * OutputFifo::kStallNs), "paused is not stalled");
    fifo.play(t0 + 10 * OutputFifo::kStallNs);
    CHECK(!fifo.stalled(t0 + 10 * OutputFifo::kStallNs + 1), "the watch starts again after resume");
}

// The contract under threads: one producer, one consumer, nothing shared but the fifo.
void testProducerAndConsumerThreads() {
    const int total = 400000;
    OutputFifo fifo(48000, 2, 2048);
    const std::vector<float> signal = stereo(0, total);
    std::atomic<bool> giveUp{false};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

    std::thread producer([&] {
        Lcg rng{11u};
        int written = 0;
        while (written < total && !giveUp.load()) {
            const int chunk = std::min(1 + rng.next(900), total - written);
            const int taken = fifo.writeFloat(signal.data() + static_cast<size_t>(written) * 2, chunk);
            written += taken;
            if (taken == 0) std::this_thread::yield();
        }
    });

    int heard = 0;
    int badFrames = 0;
    std::thread consumer([&] {
        Lcg rng{13u};
        std::vector<float> got(static_cast<size_t>(512) * 2);
        while (heard < total && !giveUp.load()) {
            const int burst = 1 + rng.next(511);
            fifo.read(got.data(), burst);
            bool silence = false;
            for (int i = 0; i < burst; ++i) {
                const float left = got[static_cast<size_t>(i) * 2];
                const float right = got[static_cast<size_t>(i) * 2 + 1];
                if (left == 0.0f && right == 0.0f) {
                    silence = true;
                    continue;
                }
                if (silence || left != static_cast<float>(heard + 1) || right != -left) ++badFrames;
                ++heard;
            }
            if (std::chrono::steady_clock::now() > deadline) giveUp.store(true);
        }
    });

    producer.join();
    consumer.join();
    CHECK(!giveUp.load(), "the threads did not finish in time");
    CHECK(heard == total, "heard %d of %d", heard, total);
    CHECK(badFrames == 0, "%d frames out of order, repeated or torn", badFrames);
    CHECK(fifo.framesWritten() == total && fifo.framesPlayed() == total, "counts: %lld written, %lld played",
          static_cast<long long>(fifo.framesWritten()), static_cast<long long>(fifo.framesPlayed()));
}

// A flush from the producer while the consumer runs: nothing written before it is heard after the
// consumer has seen it, nothing written after it is lost, and no torn frame is ever played.
void testFlushUnderAThread() {
    const int rounds = 300;
    const int perRound = 1500;
    OutputFifo fifo(48000, 2, 2048);
    std::atomic<bool> done{false};
    std::atomic<int> torn{0};
    std::atomic<int> backwards{0};

    std::thread consumer([&] {
        std::vector<float> got(static_cast<size_t>(256) * 2);
        float lastLeft = 0.0f;
        while (!done.load()) {
            fifo.read(got.data(), 256);
            for (int i = 0; i < 256; ++i) {
                const float left = got[static_cast<size_t>(i) * 2];
                const float right = got[static_cast<size_t>(i) * 2 + 1];
                if (left == 0.0f && right == 0.0f) continue;
                if (right != -left) torn.fetch_add(1);
                // The signal only ever counts up, across flushes too.
                if (left <= lastLeft) backwards.fetch_add(1);
                lastLeft = left;
            }
        }
    });

    int next = 0;
    for (int round = 0; round < rounds; ++round) {
        const std::vector<float> block = stereo(next, perRound);
        int offset = 0;
        int spins = 0;
        while (offset < perRound && spins < 1'000'000) {
            const int taken = fifo.writeFloat(block.data() + static_cast<size_t>(offset) * 2, perRound - offset);
            offset += taken;
            if (taken == 0) {
                ++spins;
                std::this_thread::yield();
            }
        }
        CHECK(offset == perRound, "round %d: wrote %d of %d", round, offset, perRound);
        next += perRound;
        // Every other round lets the consumer into the block first, so a flush lands mid-stream too.
        for (int waits = 0; round % 2 == 0 && fifo.pendingFrames() > 500 && waits < 1'000'000; ++waits) {
            std::this_thread::yield();
        }
        fifo.flush();
        CHECK(fifo.framesWritten() == 0 && fifo.pendingFrames() == 0, "round %d: flushed", round);
    }
    done.store(true);
    consumer.join();
    CHECK(torn.load() == 0, "%d torn frames", torn.load());
    CHECK(backwards.load() == 0, "%d frames out of order", backwards.load());
}

}  // namespace

int main() {
    testFormats();
    testOddChunksLoseAndRepeatNothing();
    testFullFifoTakesWhatFits();
    testEncodings();
    testUnderrunIsSilenceAndCountedOnce();
    testFlush();
    testPositionAt(44100);
    testPositionAt(48000);
    testPositionAt(96000);
    testAgedFutureAndMissingTimestamps();
    testStopEndsWithoutAStream();
    testStopPlaysOutAndIgnoresPause();
    testVolumeRamp();
    testStall();
    testProducerAndConsumerThreads();
    testFlushUnderAThread();
    if (failures) {
        std::fprintf(stderr, "output_fifo_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("output_fifo_test: ok\n");
    return EXIT_SUCCESS;
}
