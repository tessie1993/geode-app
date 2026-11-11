// Host test for speaker sync (sound/interp/SpeakerSync, #349, #488): a live renderer hears the player's
// ring when the speaker plays it, not when the sink took it. The real ring, cursor and playout clock,
// behind a fake sink that behaves like Media3's DefaultAudioSink: it fills its buffer at once, keeps it
// topped up in 10 ms buffers while it plays, and reports its position (output latency included) every
// 10 ms. Every frame written holds its own ring position, so a reader's samples say where it stands.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/speaker_sync_test.cpp
//   core/audio/ingest/PcmRing.cpp core/sound/interp/PlayoutClock.cpp -o speaker_sync_test && ./speaker_sync_test
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "audio/ingest/PcmRing.hpp"
#include "sound/interp/PlayoutClock.hpp"
#include "sound/interp/SpeakerSync.hpp"

using geode::audio::ingest::Cursor;
using geode::audio::ingest::PcmRing;
using geode::sound::PlayoutClock;
using geode::sound::interp::kNewest;
using geode::sound::interp::kPlayerRingFrames;
using geode::sound::interp::kPlayerRingRunway;
using geode::sound::interp::listenLimit;

static_assert(kNewest == Cursor::kNewest, "listenLimit's newest is the cursor's");
// The backlog a reader at the speaker has at 192 kHz and 2x over Bluetooth: 2.2 s of source frames.
static_assert(kPlayerRingFrames - 2 * kPlayerRingRunway >= 192'000 * 22 / 10, "the player's ring is overrun");

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int64_t kMs = 1'000'000;     // ns per ms
constexpr int64_t kTickNs = 10 * kMs;  // the sink's buffers and position reports

struct Sink {
    int rate;
    int64_t bufferMs;
    int64_t linkMs;
    PcmRing ring;
    PlayoutClock clock;
    int64_t nowNs = 1'000 * kMs;
    int64_t startUs = 0;  // media time of this stream's first frame
    int64_t base = 0;     // its ring position
    int64_t handled = 0;  // frames the sink took of it
    double played = 0.0;  // frames the output consumed of it
    bool playing = false;
    std::vector<float> block;

    Sink(int rate, int64_t bufferMs, int64_t linkMs, int capacity = kPlayerRingFrames, int runway = kPlayerRingRunway)
        : rate(rate), bufferMs(bufferMs), linkMs(linkMs), ring(capacity, 2, runway) {}

    int chunk() const { return rate / 100; }

    // Frames of this stream the listener has heard: what the output consumed, less the link.
    double heard() const { return std::max(0.0, played - static_cast<double>(linkMs * rate / 1000)); }
    // The ring position the speaker plays now.
    int64_t heardFrame() const { return base + static_cast<int64_t>(heard()); }

    void fill() {
        const int64_t want = bufferMs * rate / 1000;
        while (handled - static_cast<int64_t>(played) < want) {
            const int64_t before = ring.headFrames();
            block.resize(static_cast<size_t>(chunk()) * 2);
            for (int i = 0; i < chunk(); ++i) {
                block[static_cast<size_t>(i) * 2] = static_cast<float>(before + i);
                block[static_cast<size_t>(i) * 2 + 1] = static_cast<float>(before + i);
            }
            ring.writeFloat(block.data(), chunk(), 2, rate);
            clock.bufferHandled(startUs + handled * 1'000'000 / rate, before, ring.headFrames() - before);
            handled += chunk();
        }
    }

    void report() { clock.position(startUs + std::llround(heard() * 1e6 / rate), nowNs); }

    void advanceTo(int64_t ns) {
        if (playing) {
            played = std::min(static_cast<double>(handled), played + static_cast<double>(ns - nowNs) * rate / 1e9);
        }
        nowNs = ns;
    }

    void tick() {
        fill();
        report();
    }

    void play() {
        fill();
        playing = true;
        clock.playing(true, nowNs);
        report();
    }

    void pause() {
        playing = false;
        clock.playing(false, nowNs);
    }

    // A seek: the tap begins a new epoch, the sink is flushed and starts again at [us].
    void seek(int64_t us) {
        ring.beginEpoch();
        clock.flush();
        startUs = us;
        base = ring.headFrames();
        handled = 0;
        played = 0.0;
    }
};

struct Reader {
    Cursor cursor;
    int64_t next = -1;       // the sample the next frame must hold; -1 after a restart or skip
    int64_t firstRead = -1;  // the first sample after the last restart
    int64_t lastOld = -1;    // the newest sample read before it
    int64_t newest = -1;     // the newest sample read
    bool fresh = false;      // restarted, nothing read since
    int64_t read = 0;
    int restarts = 0;
    int skips = 0;
    bool ordered = true;

    explicit Reader(const PcmRing& ring) : cursor(ring) {}

    void frame(const Sink& sink, bool playerIsSource = true, bool bypass = false) {
        const int64_t limit = listenLimit(&sink.clock, playerIsSource, bypass, sink.nowNs);
        for (int run = 0; run < 4; ++run) {
            const Cursor::Take take = cursor.take(limit);
            if (take.restarted) {
                ++restarts;
                next = -1;
                lastOld = newest;
                fresh = true;
            }
            if (take.skipped) {
                ++skips;
                next = -1;
            }
            if (take.frames == 0) break;
            for (int i = 0; i < take.frames; ++i) {
                const auto sample = static_cast<int64_t>(cursor.interleaved()[static_cast<size_t>(i) * 2]);
                if (next >= 0 && sample != next) ordered = false;
                if (fresh) firstRead = sample;
                fresh = false;
                next = sample + 1;
            }
            read += take.frames;
            newest = next - 1;
        }
    }
};

// Runs the sink and a reader drawing at [fps] for [ns]. False if the reader ever held a sample the
// speaker had not reached, give or take the 10 ms the clock runs on between two reports.
bool run(Sink& sink, Reader& reader, int fps, int64_t ns) {
    const int64_t frameNs = 1'000'000'000 / fps;
    const int64_t endNs = sink.nowNs + ns;
    int64_t nextTick = sink.nowNs + kTickNs;
    int64_t nextFrame = sink.nowNs + frameNs;
    bool behind = true;
    while (std::min(nextTick, nextFrame) <= endNs) {
        const int64_t t = std::min(nextTick, nextFrame);
        sink.advanceTo(t);
        if (t == nextTick) {
            sink.tick();
            nextTick += kTickNs;
        }
        if (t == nextFrame) {
            reader.frame(sink);
            if (reader.newest > sink.heardFrame() + sink.chunk() + 2) behind = false;
            nextFrame += frameNs;
        }
    }
    sink.advanceTo(endNs);
    return behind;
}

// How far the reader trails the speaker, in ms.
int64_t trailMs(const Sink& sink, const Reader& reader) {
    return (sink.heardFrame() - 1 - reader.newest) * 1000 / sink.rate;
}

void theStartWaitsForTheSpeaker() {
    for (const int fps : {30, 60, 120}) {
        Sink sink(48'000, 500, 0);
        Reader late(sink.ring);
        Cursor newest(sink.ring);
        sink.play();
        // The bug this fixes: the sink took half a second at once, and a reader of the newest has it all.
        check(newest.take().frames == kPlayerRingRunway, "start: the newest reader already holds the sink's buffer");
        late.frame(sink);
        check(late.read == 0, "start: nothing is heard yet, so nothing is read");
        check(run(sink, late, fps, 2'000 * kMs), "start: the reader never passes the speaker");
        check(late.ordered && late.skips == 0, "start: every frame once, in order");
        check(late.read == late.newest + 1, "start: read from the stream's first frame on");
        check(late.restarts == 1, "start: one restart, the reader's first take");
        check(trailMs(sink, late) <= 1000 / fps + 12, "start: the reader keeps up with the speaker");
        check(sink.ring.headFrames() - late.newest >= 48'000 * 480 / 1000, "start: and trails the tap by the buffer");
    }
}

void aLinkDelaysThePicture() {
    Sink sink(48'000, 500, 200);
    Reader reader(sink.ring);
    sink.play();
    check(run(sink, reader, 60, 150 * kMs), "link: the reader never passes the speaker");
    check(reader.read <= sink.chunk(), "link: the output runs, but nothing has crossed the link yet");
    check(run(sink, reader, 60, 2'000 * kMs), "link: still never past the speaker");
    check(reader.ordered && reader.skips == 0 && reader.read == reader.newest + 1, "link: every frame once");
    check(sink.ring.headFrames() - reader.newest >= 48'000 * 680 / 1000, "link: the buffer and the link behind the tap");
}

void aPauseHoldsAndAResumeGoesOn() {
    Sink sink(48'000, 500, 0);
    Reader reader(sink.ring);
    sink.play();
    check(run(sink, reader, 60, 1'000 * kMs), "pause: never past the speaker before it");
    sink.pause();
    run(sink, reader, 60, 100 * kMs);
    const int64_t held = reader.read;
    check(run(sink, reader, 60, 1'000 * kMs), "pause: never past the speaker in it");
    check(reader.read == held, "pause: the speaker stands, the reader reads nothing");
    sink.play();
    check(run(sink, reader, 60, 1'000 * kMs), "resume: never past the speaker");
    check(reader.read > held + 40'000, "resume: the reader goes on");
    check(reader.ordered && reader.skips == 0 && reader.restarts == 1, "resume: where it stopped, nothing lost");
    check(reader.read == reader.newest + 1, "resume: every frame once");
}

void aSeekDropsTheUnheardTail() {
    Sink sink(48'000, 500, 0);
    Reader reader(sink.ring);
    sink.play();
    run(sink, reader, 60, 1'000 * kMs);
    const int64_t before = reader.read;
    const int64_t lastOld = reader.newest;
    sink.seek(60'000'000);
    reader.frame(sink);
    check(reader.read == before && reader.restarts == 1, "seek: nothing is read while the sink holds nothing");
    check(lastOld < sink.base - 20'000, "seek: the old stream's buffer was never heard");
    check(run(sink, reader, 60, 1'000 * kMs), "seek: the reader never passes the speaker");
    check(reader.restarts == 2, "seek: the reader restarts once, on the new stream");
    check(reader.firstRead == sink.base, "seek: at its first frame, the unheard tail dropped");
    check(reader.ordered && reader.skips == 0, "seek: in order from there");
    check(trailMs(sink, reader) <= 1000 / 60 + 12, "seek: and keeps up");
}

void aGaplessChangeHearsTheOldTail() {
    Sink sink(48'000, 500, 0);
    Reader reader(sink.ring);
    sink.play();
    run(sink, reader, 60, 1'000 * kMs);
    // The tap's chain was flushed for the next item; the sink plays on.
    sink.ring.beginEpoch();
    const int64_t epoch = sink.ring.headFrames();
    reader.frame(sink);
    check(reader.restarts == 1 && reader.newest < epoch - 20'000, "gapless: the old tail is still playing");
    check(run(sink, reader, 60, 300 * kMs), "gapless: never past the speaker");
    check(reader.restarts == 1, "gapless: no restart before the speaker reaches the new item");
    check(run(sink, reader, 60, 1'000 * kMs), "gapless: never past the speaker after it");
    check(reader.restarts == 2 && reader.firstRead == epoch, "gapless: one restart, at the new item's first frame");
    // The frame that enters the new item drops what the old one played since the frame before.
    check(reader.skips == 0 && reader.lastOld >= epoch - 2 * 48'000 / 60, "gapless: the old tail was read to its end");
}

void hiResOverBluetoothIsNotOverrun() {
    Sink sink(192'000, 500, 250);
    Reader reader(sink.ring);
    sink.play();
    check(run(sink, reader, 60, 4'000 * kMs), "192 kHz: the reader never passes the speaker");
    check(reader.skips == 0 && reader.restarts == 1 && reader.ordered, "192 kHz: the ring keeps the backlog");
    check(reader.read == reader.newest + 1, "192 kHz: every frame once");
    check(trailMs(sink, reader) <= 1000 / 60 + 12, "192 kHz: and keeps up");
}

std::vector<float> frames(int64_t from, int count) {
    std::vector<float> out(static_cast<size_t>(count) * 2);
    for (int i = 0; i < count; ++i) {
        out[static_cast<size_t>(i) * 2] = static_cast<float>(from + i);
        out[static_cast<size_t>(i) * 2 + 1] = static_cast<float>(from + i);
    }
    return out;
}

void aLimitTheRingLostFollowsTheWriter() {
    PcmRing ring(1024, 2, 256);
    for (int i = 0; i < 10; ++i) ring.writeFloat(frames(i * 256, 256).data(), 256, 2, 48'000);
    Cursor cursor(ring);
    Cursor::Take take = cursor.take(100);
    check(take.restarted && take.frames == 256, "lost limit: a take all the same");
    check(cursor.interleaved()[0] == 1792.0f, "lost limit: from the oldest frame the ring keeps");
    check(cursor.take(100).frames == 0, "lost limit: and no further until the writer moves");
    ring.writeFloat(frames(2560, 128).data(), 128, 2, 48'000);
    take = cursor.take(100);
    check(!take.skipped && take.frames == 128 && cursor.interleaved()[0] == 2048.0f, "lost limit: it follows the writer");
}

void aLimitNeverRewindsOrPassesTheHead() {
    PcmRing ring(1024, 2, 256);
    ring.writeFloat(frames(0, 200).data(), 200, 2, 48'000);
    Cursor limited(ring);
    Cursor plain(ring);
    check(limited.take(-1).frames == 0, "limit: an unknown speaker reads nothing");
    Cursor::Take take = limited.take(120);
    check(take.frames == 120 && !take.skipped && take.sampleRateHz == 48'000, "limit: up to the limit");
    check(limited.take(50).frames == 0, "limit: one behind the reader hands out nothing");
    take = limited.take(121);
    check(take.frames == 1 && limited.interleaved()[0] == 120.0f && !take.skipped, "limit: and does not rewind");
    take = limited.take(5'000);
    check(take.frames == 79 && limited.interleaved()[0] == 121.0f, "limit: one past the head stops at the head");
    check(plain.take(kNewest).frames == 200, "limit: the newest is the old take");
}

void onlyThePlayersSinkDelays() {
    Sink sink(48'000, 500, 0);
    sink.play();
    check(listenLimit(&sink.clock, true, false, sink.nowNs) == sink.base, "source: the player waits for its sink");
    check(listenLimit(&sink.clock, false, false, sink.nowNs) == kNewest, "source: the mic and capture have no sink");
    check(listenLimit(&sink.clock, true, true, sink.nowNs) == kNewest, "source: Cast plays the ring as written");
    check(listenLimit(nullptr, true, false, sink.nowNs) == kNewest, "source: a ring with no clock reads the newest");
    PlayoutClock idle;
    check(listenLimit(&idle, true, false, 0) == PlayoutClock::kUnknown, "source: a sink that took nothing is silent");
    Reader mic(sink.ring);
    mic.frame(sink, false);
    check(mic.read == kPlayerRingRunway, "source: so the mic's reader takes the newest at once");
}

// Another app's audio in the player's ring (audio/PlaybackCapture): the player paused, the ring handed over
// (a new epoch, the player no longer its source), a capture thread writing 1024-frame blocks at 48 kHz. The
// paused sink's clock still answers a speaker sample behind the capture's first frame; a reader held to it
// would read nothing for as long as the capture runs. Released, it hears every captured frame, in order.
void aCaptureIsHeardWhileThePlayerIsPaused() {
    Sink sink(44'100, 500, 0);
    Reader reader(sink.ring);
    sink.play();
    run(sink, reader, 60, 1'000 * kMs);
    sink.pause();
    sink.ring.beginEpoch();
    const int64_t first = sink.ring.headFrames();
    std::vector<float> block(1024 * 2);
    double owed = 0.0;
    for (int frame = 0; frame < 300; ++frame) {
        owed += 48'000.0 / 60.0;
        for (; owed >= 1024.0; owed -= 1024.0) {
            const int64_t at = sink.ring.headFrames();
            for (size_t i = 0; i < 1024; ++i) block[i * 2] = block[i * 2 + 1] = static_cast<float>(at) + static_cast<float>(i);
            sink.ring.writeFloat(block.data(), 1024, 2, 48'000);
        }
        sink.advanceTo(sink.nowNs + 16'666'667);
        check(listenLimit(&sink.clock, true, false, sink.nowNs) < first, "capture: the paused sink names no captured frame");
        reader.frame(sink, false);
    }
    check(reader.firstRead == first, "capture: the reader starts at the capture's first frame");
    check(reader.ordered && reader.skips == 0, "capture: every captured frame once, in order");
    check(reader.newest == sink.ring.headFrames() - 1, "capture: the reader is at the newest captured frame");
}

void theLagIsWhatTheSinkStillHolds() {
    PlayoutClock idle;
    check(idle.lagMs(0) == 0, "lag: nothing handled, nothing to wait for");
    Sink sink(48'000, 500, 200);
    sink.fill();
    check(sink.clock.lagMs(sink.nowNs) == 500, "lag: a full buffer before the output starts");
    Reader reader(sink.ring);
    sink.play();
    run(sink, reader, 60, 2'000 * kMs);
    const int lag = sink.clock.lagMs(sink.nowNs);
    check(lag >= 680 && lag <= 720, "lag: the buffer and the link while playing");
    sink.pause();
    sink.advanceTo(sink.nowNs + 5'000 * kMs);
    check(std::abs(sink.clock.lagMs(sink.nowNs) - lag) <= 1, "lag: stands in a pause");
    sink.clock.flush();
    check(sink.clock.lagMs(sink.nowNs) == 0, "lag: a flush empties the sink");

    PlayoutClock far;
    far.bufferHandled(0, 0, 480);
    far.bufferHandled(10'000'000, 480, 480);
    far.playing(true, 0);
    far.position(0, 0);
    check(far.lagMs(0) == PlayoutClock::kMaxLagMs, "lag: capped");
    far.speed(2.0f);
    far.bufferHandled(10'010'000, 960, 480);
    far.position(9'020'000, 0);
    check(far.lagMs(0) == 500, "lag: wall time, so half the media time at 2x");
}

void aRetryAddsItsFramesToTheBuffer() {
    PlayoutClock clock;
    clock.bufferHandled(0, 0, 480);
    clock.bufferHandled(10'000, 480, 0);    // the sink's first try: the tap has written nothing yet
    clock.bufferHandled(10'000, 480, 200);  // then part of it
    clock.bufferHandled(10'000, 680, 280);  // and the rest
    clock.playing(true, 0);
    clock.position(15'000, 0);
    check(clock.speakerFrame(0) == 720, "retry: 5 ms into the buffer its tries wrote");
    clock.position(30'000, 0);
    check(clock.speakerFrame(0) == 960, "retry: to its end, and no further");
}

}  // namespace

int main() {
    theStartWaitsForTheSpeaker();
    aLinkDelaysThePicture();
    aPauseHoldsAndAResumeGoesOn();
    aSeekDropsTheUnheardTail();
    aGaplessChangeHearsTheOldTail();
    hiResOverBluetoothIsNotOverrun();
    aLimitTheRingLostFollowsTheWriter();
    aLimitNeverRewindsOrPassesTheHead();
    onlyThePlayersSinkDelays();
    aCaptureIsHeardWhileThePlayerIsPaused();
    theLagIsWhatTheSinkStillHolds();
    aRetryAddsItsFramesToTheBuffer();
    if (failures) {
        std::printf("%d speaker sync check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("speaker sync: all checks passed\n");
    return EXIT_SUCCESS;
}
