// Host test for core/audio/ingest/PcmRing: the native PCM ring and its cursor, and their C ABI.
//
// The README's proof: PCM written in random chunk sizes with a rate change mid-stream reaches the reader
// every sample once, in order, each with the rate it was written at; here single-threaded and with a
// writer thread. Plus the cases the Kotlin SoundCursorTest covered before the ring moved to C++.
// And what must hold when the easy case breaks: a reader the writer laps never gets a torn take, an
// epoch begun on a third thread, more format changes than the mark log keeps, two writers at once.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore -Icore/tests/stub core/tests/pcm_ring_test.cpp
//   core/audio/ingest/PcmRing.cpp core/audio/ingest/AiffReader.cpp core/audio/ingest/TrackFeed.cpp
//   core/api/geode_ingest_api.cpp -o pcm_ring_test && ./pcm_ring_test
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/PcmRing.hpp"
#include "sound/PlayerSignals.hpp"

using geode::audio::ingest::Cursor;
using geode::audio::ingest::PcmRing;

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

// Frame f of a stereo test signal: left f, right -f (exact in a float up to 2^24).
std::vector<float> stereo(int from, int frames) {
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        out[static_cast<size_t>(i) * 2] = static_cast<float>(from + i);
        out[static_cast<size_t>(i) * 2 + 1] = -static_cast<float>(from + i);
    }
    return out;
}

void writeStereo(PcmRing& ring, int from, int frames, int rate = 48000) {
    const std::vector<float> block = stereo(from, frames);
    const PcmRing::Written written = ring.writeFloat(block.data(), frames, 2, rate);
    CHECK(written.frames == frames, "wrote %d of %d", written.frames, frames);
}

// A small deterministic generator, so a failure repeats.
struct Lcg {
    uint32_t state;
    int next(int bound) {
        state = state * 1664525u + 1013904223u;
        return static_cast<int>((state >> 8) % static_cast<uint32_t>(bound));
    }
};

// The rate a frame of the two-change streams below is written at.
constexpr int kFirstChange = 70001;
constexpr int kSecondChange = 150000;
int rateOf(int frame) { return frame < kFirstChange ? 44100 : (frame < kSecondChange ? 48000 : 96000); }
// Frames from `frame` that share its rate.
int runLeft(int frame, int total) {
    const int end = frame < kFirstChange ? kFirstChange : (frame < kSecondChange ? kSecondChange : total);
    return (end < total ? end : total) - frame;
}

// Checks one take against the stream: the next frames in order, both channels, one rate. Returns the new count.
int checkTake(const Cursor& cursor, const Cursor::Take& take, int seen) {
    const float* pcm = cursor.interleaved();
    for (int i = 0; i < take.frames; ++i) {
        const int frame = seen + i;
        if (pcm[i * 2] != static_cast<float>(frame) || pcm[i * 2 + 1] != -static_cast<float>(frame)) {
            CHECK(false, "frame %d read as %g, %g", frame, pcm[i * 2], pcm[i * 2 + 1]);
            break;
        }
        if (take.sampleRateHz != rateOf(frame)) {
            CHECK(false, "frame %d handed out at %d Hz, written at %d", frame, take.sampleRateHz, rateOf(frame));
            break;
        }
    }
    return seen + take.frames;
}

// --- The Kotlin SoundCursorTest, ported -----------------------------------------------------------

void testHandsOutEachFrameExactlyOnce() {
    PcmRing ring(1024, 2);
    Cursor cursor(ring);
    writeStereo(ring, 0, 100);
    const Cursor::Take first = cursor.take();
    CHECK(first.restarted, "first take did not report a restart");
    CHECK(first.frames == 100, "first take: %d", first.frames);
    writeStereo(ring, 100, 50);
    const Cursor::Take second = cursor.take();
    CHECK(!second.restarted, "second take restarted");
    CHECK(second.frames == 50, "second take: %d", second.frames);
    CHECK(second.sampleRateHz == 48000, "rate %d", second.sampleRateHz);
    CHECK(cursor.interleaved()[0] == 100.0f, "left %g", cursor.interleaved()[0]);
    CHECK(cursor.interleaved()[1] == -100.0f, "right %g", cursor.interleaved()[1]);
    CHECK(cursor.interleaved()[98] == 149.0f, "last left %g", cursor.interleaved()[98]);
    const Cursor::Take third = cursor.take();
    CHECK(third.frames == 0 && !third.restarted && third.sampleRateHz == 0, "an empty take was not empty");
}

void testNewEpochRestarts() {
    PcmRing ring(1024, 2);
    Cursor cursor(ring);
    writeStereo(ring, 0, 100);
    cursor.take();
    CHECK(ring.writtenFrames() == 100, "written %lld", static_cast<long long>(ring.writtenFrames()));
    ring.beginEpoch();
    CHECK(ring.writtenFrames() == 0, "an epoch did not restart the count");
    writeStereo(ring, 0, 10);
    const Cursor::Take take = cursor.take();
    CHECK(take.restarted, "a new epoch did not restart the cursor");
    CHECK(take.frames == 10, "took %d", take.frames);
    CHECK(cursor.interleaved()[0] == 0.0f, "first %g", cursor.interleaved()[0]);
    // A cursor that never read the old epoch must not get it either.
    Cursor late(ring);
    const Cursor::Take all = late.take();
    CHECK(all.restarted && all.frames == 10, "a late cursor took %d", all.frames);
    // An epoch with nothing written yet: a restart, no frames, then nothing more.
    ring.beginEpoch();
    const Cursor::Take empty = cursor.take();
    CHECK(empty.restarted && empty.frames == 0, "empty epoch: %d frames", empty.frames);
    CHECK(!cursor.take().restarted, "restarted twice for one epoch");
}

void testStallKeepsTheNewestAudio() {
    PcmRing ring(1024, 2, 256);
    Cursor cursor(ring, 256);
    writeStereo(ring, 0, 10);
    cursor.take();
    for (int i = 0; i < 4; ++i) writeStereo(ring, 10 + i * 200, 200);
    const Cursor::Take take = cursor.take();
    CHECK(take.frames == 256, "took %d", take.frames);
    CHECK(!take.restarted, "a stall restarted the cursor");
    CHECK(take.skipped, "a stall was not reported as a skip");
    CHECK(cursor.interleaved()[0] == static_cast<float>(810 - 256), "first %g", cursor.interleaved()[0]);
}

void testMonoSourceFeedsBothChannels() {
    PcmRing ring(1024, 2);
    Cursor cursor(ring);
    const float mono[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    ring.writeFloat(mono, 4, 1, 22050);
    const Cursor::Take take = cursor.take();
    CHECK(take.frames == 4 && take.sampleRateHz == 22050, "took %d at %d", take.frames, take.sampleRateHz);
    CHECK(cursor.interleaved()[0] == 1.0f && cursor.interleaved()[1] == 1.0f, "first frame not on both channels");
    CHECK(cursor.interleaved()[7] == 4.0f, "last %g", cursor.interleaved()[7]);
    // The level check reads the ring as stored: the second channel of a mono source is silent.
    float latest[8] = {};
    CHECK(ring.snapshotLatest(latest, 4), "no snapshot");
    CHECK(latest[0] == 1.0f && latest[1] == 0.0f && latest[6] == 4.0f, "snapshot %g %g %g", latest[0], latest[1], latest[6]);
}

// --- Sizes, encodings, refusals -------------------------------------------------------------------

void testValidSizes() {
    CHECK(PcmRing::valid(1024, 2, 0), "a plain ring refused");
    CHECK(PcmRing::valid(4, 1, 0) && PcmRing::valid(1024, 2, 1024), "edge sizes refused");
    CHECK(!PcmRing::valid(1000, 2, 0), "capacity not a power of two");
    CHECK(!PcmRing::valid(0, 2, 0) && !PcmRing::valid(-8, 2, 0), "no capacity");
    CHECK(!PcmRing::valid(1024, 0, 0), "no channels");
    CHECK(!PcmRing::valid(1024, 2, 2048) && !PcmRing::valid(1024, 2, -1), "a runway outside the ring");
    CHECK(!PcmRing::valid(2, 2, 0), "a default runway of no frames");
    PcmRing ring(1 << 16, 2);
    CHECK(ring.capacityFrames() == 65536 && ring.channelCount() == 2, "sizes");
    CHECK(ring.maxWriteFrames() == 16384, "default runway %d", ring.maxWriteFrames());
    CHECK(Cursor(ring).maxFrames() == 16384 && Cursor(ring, 100).maxFrames() == 100, "cursor sizes");
}

void testEncodings() {
    PcmRing ring(64, 2);
    Cursor cursor(ring);
    // 16-bit by 1/32768, as the Kotlin tap and capture pump scaled it.
    const int16_t shorts[4] = {-32768, 16384, 32767, 0};
    const PcmRing::Written s16 = ring.writeInt16(shorts, 2, 2, 44100);
    CHECK(s16.frames == 2 && s16.peak == 1.0f, "s16 wrote %d, peak %g", s16.frames, s16.peak);
    cursor.take();
    CHECK(cursor.interleaved()[0] == -1.0f && cursor.interleaved()[1] == 0.5f, "s16 %g %g", cursor.interleaved()[0], cursor.interleaved()[1]);
    CHECK(cursor.interleaved()[2] == 32767.0f / 32768.0f && cursor.interleaved()[3] == 0.0f, "s16 second frame");
    // Unsigned 8-bit about 128, as the output-mix capture scaled it; mono on both channels.
    const uint8_t bytes[3] = {0, 128, 192};
    const PcmRing::Written u8 = ring.writeBytes(bytes, 3, geode::audio::ingest::kUnsigned8, 1, 44100);
    CHECK(u8.frames == 3 && u8.peak == 1.0f, "u8 wrote %d, peak %g", u8.frames, u8.peak);
    cursor.take();
    CHECK(cursor.interleaved()[0] == -1.0f && cursor.interleaved()[2] == 0.0f && cursor.interleaved()[4] == 0.5f, "u8 values");
    CHECK(cursor.interleaved()[5] == 0.5f, "u8 mono not on both channels");
    // Raw bytes at an odd address, with a trailing half frame: whole frames only.
    unsigned char raw[1 + 2 * 3 * 2 + 3] = {};
    const int16_t six[6] = {100, -100, 200, -200, 300, -300};
    std::memcpy(raw + 1, six, sizeof(six));
    const PcmRing::Written fromBytes = ring.writeBytes(raw + 1, sizeof(six) + 3, geode::audio::ingest::kSigned16, 2, 44100);
    CHECK(fromBytes.frames == 3, "bytes wrote %d", fromBytes.frames);
    cursor.take();
    CHECK(cursor.interleaved()[4] == 300.0f / 32768.0f && cursor.interleaved()[5] == -300.0f / 32768.0f, "s16 bytes");
    const float floats[2] = {0.25f, -0.75f};
    const PcmRing::Written f32 = ring.writeBytes(floats, sizeof(floats), geode::audio::ingest::kFloat32, 2, 44100);
    CHECK(f32.frames == 1 && f32.peak == 0.75f, "float bytes wrote %d, peak %g", f32.frames, f32.peak);
    cursor.take();
    CHECK(cursor.interleaved()[0] == 0.25f && cursor.interleaved()[1] == -0.75f, "float bytes");
    // More source channels than the ring keeps: the first two, and the peak is of what was stored.
    const float surround[6] = {0.1f, 0.2f, 0.9f, 0.9f, 0.9f, 0.9f};
    const PcmRing::Written wide = ring.writeFloat(surround, 1, 6, 44100);
    CHECK(wide.frames == 1 && wide.peak == 0.2f, "6 channels wrote %d, peak %g", wide.frames, wide.peak);
    const Cursor::Take take = cursor.take();
    CHECK(take.frames == 1 && cursor.interleaved()[0] == 0.1f && cursor.interleaved()[1] == 0.2f, "6 channels read");
}

void testRefusedBlocks() {
    PcmRing ring(64, 2);
    const float one[2] = {1.0f, 1.0f};
    CHECK(ring.writeFloat(nullptr, 1, 2, 48000).frames == 0, "null block written");
    CHECK(ring.writeFloat(one, 0, 2, 48000).frames == 0, "empty block written");
    CHECK(ring.writeFloat(one, 1, 0, 48000).frames == 0, "no channels written");
    CHECK(ring.writeFloat(one, 1, 2, 0).frames == 0, "no rate written");
    CHECK(ring.writeBytes(one, sizeof(one), 21, 2, 48000).frames == 0, "unknown encoding written");
    CHECK(ring.writeBytes(one, sizeof(one), geode::audio::ingest::kFloat32, 0, 48000).frames == 0, "bytes with no channels");
    CHECK(ring.writtenFrames() == 0, "a refused block moved the head");
    float out[4];
    CHECK(!ring.snapshotLatest(out, 2), "a snapshot of nothing");
    CHECK(!ring.snapshotLatest(out, 128), "a snapshot larger than the ring");
}

// A block longer than the runway lands whole, and a block longer than the ring leaves its newest part.
void testLongBlocks() {
    PcmRing ring(1024, 2, 256);
    Cursor cursor(ring, 1024);
    writeStereo(ring, 0, 700);
    Cursor::Take take = cursor.take();
    CHECK(take.frames == 700 && cursor.interleaved()[0] == 0.0f, "long block: %d", take.frames);
    CHECK(cursor.interleaved()[699 * 2] == 699.0f, "long block end %g", cursor.interleaved()[699 * 2]);
    writeStereo(ring, 700, 3000);
    take = cursor.take();
    // The oldest frame still safe to read is a runway short of a full ring behind the head.
    CHECK(take.frames == 1024 - 256, "after a lap took %d", take.frames);
    CHECK(cursor.interleaved()[0] == static_cast<float>(3700 - (1024 - 256)), "after a lap first %g", cursor.interleaved()[0]);
}

// --- The README's proof ---------------------------------------------------------------------------

void testRandomChunksAndRateChanges() {
    constexpr int kTotal = 200000;
    PcmRing ring(4096, 2);
    Cursor cursor(ring);
    const int most = cursor.maxFrames();
    Lcg rng{12345u};
    int written = 0;
    int seen = 0;
    int takes = 0;
    bool first = true;
    const auto takeOnce = [&] {
        const Cursor::Take take = cursor.take();
        CHECK(take.restarted == first, "take %d restarted = %d", takes, take.restarted ? 1 : 0);
        first = false;
        ++takes;
        seen = checkTake(cursor, take, seen);
        return take.frames;
    };
    while (written < kTotal) {
        int chunk = 1 + rng.next(700);
        if (chunk > kTotal - written) chunk = kTotal - written;
        // One write has one rate: a chunk ends where the stream's rate changes.
        if (chunk > runLeft(written, kTotal)) chunk = runLeft(written, kTotal);
        // The reader keeps up: never further behind than one take holds.
        while (written - seen + chunk > most) {
            if (takeOnce() == 0) break;
        }
        writeStereo(ring, written, chunk, rateOf(written));
        written += chunk;
        if (rng.next(3) == 0) takeOnce();
    }
    while (takeOnce() > 0) {
    }
    CHECK(seen == kTotal, "saw %d of %d frames", seen, kTotal);
    CHECK(ring.writtenFrames() == kTotal, "written %lld", static_cast<long long>(ring.writtenFrames()));
}

// A take that straddles a rate change ends at it, and the rest comes with the next take.
void testTakeEndsAtRateChange() {
    PcmRing ring(1024, 2);
    Cursor cursor(ring);
    writeStereo(ring, 0, 30, 44100);
    writeStereo(ring, 30, 20, 48000);
    const float mono[5] = {7.0f, 7.0f, 7.0f, 7.0f, 7.0f};
    ring.writeFloat(mono, 5, 1, 48000);
    Cursor::Take take = cursor.take();
    CHECK(take.frames == 30 && take.sampleRateHz == 44100, "first run: %d at %d", take.frames, take.sampleRateHz);
    take = cursor.take();
    CHECK(take.frames == 20 && take.sampleRateHz == 48000, "second run: %d at %d", take.frames, take.sampleRateHz);
    CHECK(cursor.interleaved()[0] == 30.0f && cursor.interleaved()[1] == -30.0f, "second run starts at %g", cursor.interleaved()[0]);
    // The same rate from a mono source is its own run: its frames, and only its frames, go to both channels.
    take = cursor.take();
    CHECK(take.frames == 5 && take.sampleRateHz == 48000, "mono run: %d at %d", take.frames, take.sampleRateHz);
    CHECK(cursor.interleaved()[0] == 7.0f && cursor.interleaved()[1] == 7.0f, "mono run not on both channels");
    CHECK(cursor.take().frames == 0, "frames left over");
}

// More rate changes than a reader can fall behind by in practice, each one read back with its own rate.
void testManyRateChanges() {
    PcmRing ring(1024, 2);
    Cursor cursor(ring);
    int seen = 0;
    for (int round = 0; round < 100; ++round) {
        const int rate = 8000 + round * 100;
        writeStereo(ring, round * 10, 10, rate);
        // Read every third round, so a take meets several changes at once.
        if (round % 3 != 2) continue;
        for (;;) {
            const Cursor::Take take = cursor.take();
            if (take.frames == 0) break;
            CHECK(take.frames == 10, "run of %d", take.frames);
            CHECK(cursor.interleaved()[0] == static_cast<float>(seen), "run starts at %g, not %d", cursor.interleaved()[0], seen);
            CHECK(take.sampleRateHz == 8000 + (seen / 10) * 100, "frame %d at %d Hz", seen, take.sampleRateHz);
            seen += take.frames;
        }
    }
    CHECK(seen == 990, "saw %d", seen);
}

// Readers do not change the ring: two cursors at their own pace each get every frame.
void testTwoReaders() {
    PcmRing ring(1024, 2);
    Cursor fast(ring);
    Cursor slow(ring);
    int fastSeen = 0;
    int slowSeen = 0;
    for (int block = 0; block < 40; ++block) {
        writeStereo(ring, block * 50, 50);
        const Cursor::Take a = fast.take();
        CHECK(a.frames == 50 && fast.interleaved()[0] == static_cast<float>(fastSeen), "fast reader at block %d", block);
        fastSeen += a.frames;
        if (block % 4 == 3) {
            const Cursor::Take b = slow.take();
            CHECK(b.frames == 200 && slow.interleaved()[0] == static_cast<float>(slowSeen), "slow reader at block %d", block);
            slowSeen += b.frames;
        }
    }
    CHECK(fastSeen == 2000 && slowSeen == 2000, "readers saw %d and %d", fastSeen, slowSeen);
    // reset(): the next take starts over from the newest audio.
    fast.reset();
    const Cursor::Take again = fast.take();
    CHECK(again.restarted && again.frames == 256, "after reset: %d frames", again.frames);
    CHECK(fast.interleaved()[0] == static_cast<float>(2000 - 256), "after reset first %g", fast.interleaved()[0]);
}

// The same proof with the writer on its own thread, as the player's tap and the capture pumps are.
void testWriterThread() {
    constexpr int kTotal = 300000;
    PcmRing ring(4096, 2);
    Cursor cursor(ring);
    const int most = cursor.maxFrames();
    std::atomic<int> seenShared{0};
    std::thread writer([&] {
        Lcg rng{777u};
        int written = 0;
        while (written < kTotal) {
            int chunk = 1 + rng.next(512);
            if (chunk > kTotal - written) chunk = kTotal - written;
            if (chunk > runLeft(written, kTotal)) chunk = runLeft(written, kTotal);
            // The reader is never left further behind than one take holds, so it may not skip.
            while (written - seenShared.load() + chunk > most) std::this_thread::yield();
            const std::vector<float> block = stereo(written, chunk);
            ring.writeFloat(block.data(), chunk, 2, rateOf(written));
            written += chunk;
        }
    });
    int seen = 0;
    bool first = true;
    bool restartedLater = false;
    while (seen < kTotal && failures == 0) {
        const Cursor::Take take = cursor.take();
        if (!first && take.restarted) restartedLater = true;
        first = false;
        seen = checkTake(cursor, take, seen);
        seenShared.store(seen);
        if (take.frames == 0) std::this_thread::yield();
    }
    // A failed check stops the reader early: let the writer run out before joining it.
    seenShared.store(kTotal + most);
    writer.join();
    CHECK(seen == kTotal, "saw %d of %d frames", seen, kTotal);
    CHECK(!restartedLater, "the cursor restarted without an epoch");
}

// --- Readers and writers that break the easy case -------------------------------------------------

// True when a take's frames are consecutive frames of the stereo test signal: a copy the writer lapped
// would hold frames of two laps and fail this.
bool whole(const Cursor& cursor, const Cursor::Take& take) {
    const float* pcm = cursor.interleaved();
    for (int i = 0; i < take.frames; ++i) {
        if (pcm[i * 2] != pcm[0] + static_cast<float>(i) || pcm[i * 2 + 1] != -pcm[i * 2]) return false;
    }
    return true;
}

// Writes `total` frames of the stereo test signal in random chunks as fast as it can, telling `written`
// how far it is after each block.
void writeUnthrottled(PcmRing& ring, int total, uint32_t seed, std::atomic<int>& written) {
    Lcg rng{seed};
    int at = 0;
    while (at < total) {
        int chunk = 1 + rng.next(256);
        if (chunk > total - at) chunk = total - at;
        const std::vector<float> block = stereo(at, chunk);
        ring.writeFloat(block.data(), chunk, 2, 48000);
        at += chunk;
        written.store(at);
    }
}

// A reader the writer laps: a small ring and a writer that never waits. Every take is whole, moves
// forward, and follows the last one unless it says it restarted or skipped; nothing torn gets out.
void testLappedReader() {
    constexpr int kTotal = 4000000;
    PcmRing ring(1024, 2, 256);
    Cursor cursor(ring);
    std::atomic<int> written{0};
    std::thread writer([&] { writeUnthrottled(ring, kTotal, 4242u, written); });
    int expected = 0;
    for (;;) {
        // Read before the take: an empty take after the writer finished is the end of the stream.
        const bool finished = written.load() == kTotal;
        const Cursor::Take take = cursor.take();
        if (take.frames > 0) {
            const int first = static_cast<int>(cursor.interleaved()[0]);
            if (!whole(cursor, take)) {
                CHECK(false, "a torn take of %d frames from frame %d", take.frames, first);
                break;
            }
            if (first < expected) {
                CHECK(false, "a take went back to frame %d after %d", first, expected);
                break;
            }
            if (first != expected && !take.restarted && !take.skipped) {
                CHECK(false, "frames %d..%d were dropped without a restart or a skip", expected, first);
                break;
            }
            expected = first + take.frames;
        }
        if (finished && take.frames == 0) break;
    }
    writer.join();
    CHECK(failures > 0 || expected == kTotal, "the reader ended at frame %d of %d", expected, kTotal);
}

// beginEpoch from a third thread while the writer writes: no take hands out a frame from before an
// epoch that had begun when the take started.
void testEpochFromThirdThread() {
    constexpr int kTotal = 1000000;
    PcmRing ring(4096, 2);
    Cursor cursor(ring);
    std::atomic<int> written{0};
    // A frame known to be in the ring before the newest epoch began: nothing below it may be read.
    std::atomic<int> floor{0};
    std::thread writer([&] { writeUnthrottled(ring, kTotal, 99u, written); });
    std::thread epochs([&] {
        while (written.load() < kTotal) {
            const int before = written.load();
            ring.beginEpoch();
            floor.store(before);
            std::this_thread::yield();
        }
    });
    int restarts = 0;
    for (;;) {
        const bool finished = written.load() == kTotal;
        const int low = floor.load();
        const Cursor::Take take = cursor.take();
        if (take.restarted) ++restarts;
        if (take.frames > 0) {
            const int first = static_cast<int>(cursor.interleaved()[0]);
            if (!whole(cursor, take)) {
                CHECK(false, "a torn take of %d frames from frame %d", take.frames, first);
                break;
            }
            if (first < low) {
                CHECK(false, "frame %d handed out after an epoch that began at or after frame %d", first, low);
                break;
            }
        }
        if (finished && take.frames == 0) break;
    }
    writer.join();
    epochs.join();
    CHECK(restarts >= 1, "the cursor never restarted");
}

// More format changes between two takes than the mark log keeps: the frames older than the log read
// with the oldest kept format, every later run with its own, and no frame is lost or repeated.
void testMarkLogWraps() {
    constexpr int kBlocks = 40;
    constexpr int kKept = 15;  // one fewer than the log holds
    PcmRing ring(1024, 2);
    Cursor cursor(ring);
    for (int b = 0; b < kBlocks; ++b) writeStereo(ring, b * 4, 4, 8000 + b);
    const int oldestKept = kBlocks - kKept;
    Cursor::Take take = cursor.take();
    CHECK(take.frames == oldestKept * 4, "the frames before the log: %d", take.frames);
    CHECK(take.sampleRateHz == 8000 + oldestKept, "the frames before the log at %d Hz", take.sampleRateHz);
    CHECK(whole(cursor, take) && cursor.interleaved()[0] == 0.0f, "the frames before the log are not frames 0 on");
    for (int b = oldestKept; b < kBlocks; ++b) {
        take = cursor.take();
        CHECK(take.frames == 4 && take.sampleRateHz == 8000 + b, "block %d: %d frames at %d Hz", b, take.frames, take.sampleRateHz);
        CHECK(whole(cursor, take) && cursor.interleaved()[0] == static_cast<float>(b * 4), "block %d starts at %g", b, cursor.interleaved()[0]);
    }
    CHECK(cursor.take().frames == 0, "frames left over");
}

// Two writers at once, which the shell prevents and the ring must survive: the head never goes back,
// and once one writer is left the reader is back on whole audio with the right rate within a few takes.
void testTwoWriters() {
    PcmRing ring(1024, 2, 256);
    Cursor cursor(ring);
    std::atomic<int> running{2};
    const auto hammer = [&](int rate, int channels, uint32_t seed) {
        Lcg rng{seed};
        const std::vector<float> block(256 * 2, 0.25f);
        for (int n = 0; n < 20000; ++n) ring.writeFloat(block.data(), 1 + rng.next(256), channels, rate);
        running.fetch_sub(1);
    };
    std::thread a([&] { hammer(44100, 2, 1u); });
    std::thread b([&] { hammer(48000, 1, 2u); });
    int64_t last = 0;
    bool wentBack = false;
    while (running.load() > 0) {
        const int64_t head = ring.writtenFrames();
        if (head < last) wentBack = true;
        last = head;
        cursor.take();
    }
    a.join();
    b.join();
    CHECK(!wentBack, "the head went backwards under two writers");
    CHECK(ring.writtenFrames() >= last && ring.writtenFrames() > 0, "the head ended at %lld", static_cast<long long>(ring.writtenFrames()));
    // At most one take per kept format mark, and one for a restart.
    int takes = 0;
    while (cursor.take().frames > 0 && takes < 64) ++takes;
    CHECK(takes < 64, "the reader did not drain after the writers stopped");
    writeStereo(ring, 0, 100, 32000);
    const Cursor::Take take = cursor.take();
    CHECK(take.frames == 100 && take.sampleRateHz == 32000, "after the overlap: %d frames at %d Hz", take.frames, take.sampleRateHz);
    CHECK(!take.restarted && !take.skipped, "after the overlap the reader was not in step");
    CHECK(whole(cursor, take) && cursor.interleaved()[0] == 0.0f, "after the overlap the block was not read back");
}

// The newest frames when they lie across the end of the storage.
void testSnapshotAcrossTheWrap() {
    PcmRing ring(64, 2);
    writeStereo(ring, 0, 16);
    writeStereo(ring, 16, 16);
    writeStereo(ring, 32, 16);
    writeStereo(ring, 48, 12);
    writeStereo(ring, 60, 10);
    float latest[16] = {};
    CHECK(ring.snapshotLatest(latest, 8), "no snapshot across the wrap");
    for (int i = 0; i < 8; ++i) {
        CHECK(latest[i * 2] == static_cast<float>(62 + i) && latest[i * 2 + 1] == -static_cast<float>(62 + i),
              "snapshot frame %d read as %g, %g", i, latest[i * 2], latest[i * 2 + 1]);
    }
    // More than the ring keeps clear of the writer: refused, not handed out half overwritten.
    float most[64 * 2];
    CHECK(!ring.snapshotLatest(most, 64), "a snapshot of slots the writer may be in");
}

// --- The C ABI ------------------------------------------------------------------------------------

void testAbi() {
    CHECK(geode_pcm_ring_create(1000, 2, 0) == nullptr, "abi made a ring that is not a power of two");
    geode_pcm_ring* ring = geode_pcm_ring_create(1024, 2, 0);
    CHECK(ring != nullptr, "abi made no ring");
    geode_pcm_cursor* cursor = geode_pcm_cursor_create(ring, 0);
    CHECK(cursor != nullptr, "abi made no cursor");
    CHECK(geode_pcm_cursor_max_frames(cursor) == 256 && geode_pcm_cursor_channels(cursor) == 2, "abi cursor sizes");

    const int16_t shorts[4] = {16384, -16384, 8192, -8192};
    float peak = -1.0f;
    CHECK(geode_pcm_ring_write_s16(ring, shorts, 2, 2, 44100, &peak) == 2, "abi s16 write");
    CHECK(peak == 0.5f, "abi peak %g", peak);
    CHECK(geode_pcm_ring_written(ring) == 2, "abi written");
    std::vector<float> out(256 * 2);
    int restarted = -1;
    int rate = -1;
    CHECK(geode_pcm_cursor_take(cursor, out.data(), 8, &restarted, &rate) == -1, "abi took into a short buffer");
    CHECK(geode_pcm_cursor_take(cursor, out.data(), 256, &restarted, &rate) == 2, "abi take");
    CHECK(restarted == 1 && rate == 44100, "abi take: restarted %d, rate %d", restarted, rate);
    CHECK(out[0] == 0.5f && out[1] == -0.5f && out[2] == 0.25f, "abi take values");

    // 16-bit out: clamped, scaled by 32767 and rounded (0.5 is 16383.5), with the rate of this block.
    const float floats[4] = {1.5f, -1.0f, 0.5f, 0.0f};
    CHECK(geode_pcm_ring_write_f32(ring, floats, 2, 2, 48000, nullptr) == 2, "abi f32 write");
    std::vector<int16_t> pcm(256 * 2);
    CHECK(geode_pcm_cursor_take_s16(cursor, pcm.data(), 256, &restarted, &rate) == 2, "abi s16 take");
    CHECK(restarted == 0 && rate == 48000, "abi s16 take: restarted %d, rate %d", restarted, rate);
    CHECK(pcm[0] == 32767 && pcm[1] == -32767 && pcm[2] == 16384 && pcm[3] == 0, "abi s16 values %d %d %d", pcm[0], pcm[1], pcm[2]);
    CHECK(geode_pcm_ring_heard_at(ring, 4096, 500, 100) == 100, "abi heard more frames than the ring holds");

    const uint8_t bytes[2] = {255, 0};
    CHECK(geode_pcm_ring_write_bytes(ring, bytes, 2, GEODE_PCM_U8, 1, 48000, &peak) == 2, "abi u8 write");
    CHECK(peak == 1.0f, "abi u8 peak %g", peak);
    CHECK(geode_pcm_ring_write_bytes(ring, bytes, 2, 99, 1, 48000, &peak) == 0 && peak == 0.0f, "abi unknown encoding");

    // The hand-over: a capture takes the ring, so the tap writes and flushes nothing and the engines hear no
    // player; handed back, the tap writes again. Kills: the tap still writing, no epoch, the source flag left.
    {
        const int16_t tapBlock[4] = {1000, -1000, 2000, -2000};
        geode_pcm_ring_hand_over(ring, 0);
        CHECK(!geode::sound::player::playerIsSource(), "hand-over: the player still counts as the source");
        CHECK(geode_pcm_ring_written(ring) == 0, "hand-over: no new epoch");
        CHECK(geode_pcm_ring_write_tap(ring, tapBlock, sizeof tapBlock, GEODE_PCM_S16, 2, 48000) == 0,
              "hand-over: the tap wrote a ring a capture has");
        CHECK(geode_capture_write(ring, tapBlock, sizeof tapBlock, GEODE_PCM_S16, 2, 48000, 7, 1) == 7,
              "hand-over: the capture's write");
        geode_pcm_ring_tap_flush(ring);
        CHECK(geode_pcm_ring_written(ring) == 2, "hand-over: the tap's flush ended the capture's epoch");
        geode_pcm_ring_hand_over(ring, 1);
        CHECK(geode::sound::player::playerIsSource(), "hand-back: the player is the source again");
        CHECK(geode_pcm_ring_written(ring) == 0, "hand-back: no new epoch");
        CHECK(geode_pcm_ring_write_tap(ring, tapBlock, sizeof tapBlock, GEODE_PCM_S16, 2, 48000) == 2,
              "hand-back: the tap writes again");
        geode_pcm_ring_tap_flush(ring);
        CHECK(geode_pcm_ring_written(ring) == 0, "hand-back: the tap's flush starts an epoch");
        geode_pcm_ring_hand_over(nullptr, 0);
        geode_pcm_ring_hand_over(nullptr, 1);  // a null handle is safe and changes nothing
        CHECK(geode_pcm_ring_write_tap(nullptr, tapBlock, sizeof tapBlock, GEODE_PCM_S16, 2, 48000) == 0, "tap, null");
    }

    // A capture's read: written like write_bytes, and the heard time moves only on a block above digital zero.
    const int64_t beforeCapture = geode_pcm_ring_written(ring);
    const int16_t zeros[4] = {0, 0, 0, 0};
    CHECK(geode_capture_write(ring, zeros, sizeof zeros, GEODE_PCM_S16, 2, 48000, 900, 100) == 100,
          "abi capture: zeros are not sound");
    CHECK(geode_pcm_ring_written(ring) == beforeCapture + 2, "abi capture: zeros still written");
    const float sound[2] = {0.0f, -0.25f};
    CHECK(geode_capture_write(ring, sound, sizeof sound, GEODE_PCM_FLOAT, 2, 48000, 900, 100) == 900,
          "abi capture: a block with sound moves the heard time");
    CHECK(geode_capture_write(nullptr, sound, sizeof sound, GEODE_PCM_FLOAT, 2, 48000, 900, 100) == 100,
          "abi capture: no ring, nothing heard");

    // Rest when silent: unchanged until the epoch holds that many frames, now once one is audible.
    geode_pcm_ring_begin_epoch(ring);
    CHECK(geode_pcm_ring_written(ring) == 0, "abi epoch");
    CHECK(geode_pcm_ring_heard_at(ring, 64, 500, 100) == 100, "abi heard an empty epoch");
    const std::vector<float> quiet(64 * 2, 0.001f);
    geode_pcm_ring_write_f32(ring, quiet.data(), 64, 2, 48000, nullptr);
    CHECK(geode_pcm_ring_heard_at(ring, 64, 500, 100) == 100, "abi heard a quiet block");
    std::vector<float> loud(64 * 2, 0.0f);
    loud[77] = 0.5f;
    geode_pcm_ring_write_f32(ring, loud.data(), 64, 2, 48000, nullptr);
    CHECK(geode_pcm_ring_heard_at(ring, 64, 500, 100) == 500, "abi missed a loud block");
    CHECK(geode_pcm_ring_heard_at(ring, 0, 500, 100) == 100, "abi heard no frames");
    geode_pcm_cursor_reset(cursor);
    CHECK(geode_pcm_cursor_take(cursor, out.data(), 256, &restarted, &rate) == 128 && restarted == 1, "abi reset");

    // A cursor shares the ring: it reads on after the handle is gone.
    geode_pcm_ring_write_f32(ring, floats, 2, 2, 48000, nullptr);
    geode_pcm_ring_destroy(ring);
    CHECK(geode_pcm_cursor_take(cursor, out.data(), 256, &restarted, &rate) == 2, "abi cursor lost its ring");
    CHECK(out[0] == 1.5f && out[2] == 0.5f, "abi values after the handle went");
    geode_pcm_cursor_destroy(cursor);

    // Every call is safe on a null handle.
    geode_pcm_ring_destroy(nullptr);
    geode_pcm_cursor_destroy(nullptr);
    geode_pcm_ring_begin_epoch(nullptr);
    geode_pcm_cursor_reset(nullptr);
    peak = -1.0f;
    CHECK(geode_pcm_ring_write_f32(nullptr, floats, 2, 2, 48000, &peak) == 0 && peak == 0.0f, "abi wrote to null");
    CHECK(geode_pcm_ring_write_s16(nullptr, shorts, 2, 2, 48000, nullptr) == 0, "abi wrote s16 to null");
    CHECK(geode_pcm_ring_write_bytes(nullptr, bytes, 2, GEODE_PCM_U8, 1, 48000, nullptr) == 0, "abi wrote bytes to null");
    CHECK(geode_pcm_ring_written(nullptr) == 0, "abi null written");
    CHECK(geode_pcm_ring_heard_at(nullptr, 64, 500, 100) == 100, "abi null heard");
    CHECK(geode_pcm_cursor_create(nullptr, 0) == nullptr, "abi cursor on null");
    CHECK(geode_pcm_cursor_max_frames(nullptr) == 0 && geode_pcm_cursor_channels(nullptr) == 0, "abi null cursor sizes");
    CHECK(geode_pcm_cursor_take(nullptr, out.data(), 256, nullptr, nullptr) == -1, "abi null take");
    CHECK(geode_pcm_cursor_take_s16(nullptr, pcm.data(), 256, nullptr, nullptr) == -1, "abi null s16 take");
}

}  // namespace

int main() {
    testHandsOutEachFrameExactlyOnce();
    testNewEpochRestarts();
    testStallKeepsTheNewestAudio();
    testMonoSourceFeedsBothChannels();
    testValidSizes();
    testEncodings();
    testRefusedBlocks();
    testLongBlocks();
    testRandomChunksAndRateChanges();
    testTakeEndsAtRateChange();
    testManyRateChanges();
    testTwoReaders();
    testWriterThread();
    testLappedReader();
    testEpochFromThirdThread();
    testMarkLogWraps();
    testTwoWriters();
    testSnapshotAcrossTheWrap();
    testAbi();
    if (failures) {
        std::fprintf(stderr, "pcm_ring_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("pcm_ring_test: ok\n");
    return EXIT_SUCCESS;
}
