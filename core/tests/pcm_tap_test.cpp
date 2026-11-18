// Host test for the player's Media3 tap on the ring (geode_pcm_ring_tap_set_owner, _flush, _write): what the shell's
// PcmTap used to decide in Kotlin. The tap writes in the format of its last flush, every flush starts a ring epoch
// before a sample of the new format is written, and a closed gate (another app's capture owns the ring) writes
// nothing and starts no epoch.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore -Icore/tests/stub core/tests/pcm_tap_test.cpp
//   core/audio/ingest/PcmRing.cpp core/audio/ingest/AiffReader.cpp core/audio/ingest/TrackFeed.cpp
//   core/audio/ingest/SnapshotStitch.cpp core/api/geode_ingest_api.cpp core/output/FrameClock.cpp
//   -o pcm_tap_test && ./pcm_tap_test
#include <cstdint>
#include <cstdio>
#include <vector>

#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"

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

constexpr int kEncodingS16 = 2;  // AudioFormat.ENCODING_PCM_16BIT
constexpr int kEncodingFloat = 4;
constexpr int kEncodingInvalid = 0;

geode_pcm_ring* makeRing() { return geode_pcm_ring_create(1 << 14, 2, 0); }

std::vector<int16_t> constantS16(int frames, int16_t value) {
    return std::vector<int16_t>(static_cast<size_t>(frames) * 2, value);
}

int tapWriteS16(geode_pcm_ring* r, const std::vector<int16_t>& block) {
    return geode_pcm_ring_tap_write(r, block.data(), block.size() * sizeof(int16_t));
}

// Nothing is written before a flush gave a format, and nothing at an unusable one.
void testNoFormatNoWrite() {
    geode_pcm_ring* r = makeRing();
    const std::vector<int16_t> block = constantS16(256, 8192);
    CHECK(tapWriteS16(r, block) == 0, "wrote before any flush");
    geode_pcm_ring_tap_flush(r, 0, 2, kEncodingS16);
    CHECK(tapWriteS16(r, block) == 0, "wrote at 0 Hz");
    geode_pcm_ring_tap_flush(r, 48000, 0, kEncodingS16);
    CHECK(tapWriteS16(r, block) == 0, "wrote with no channels");
    geode_pcm_ring_tap_flush(r, 48000, 2, kEncodingInvalid);
    CHECK(tapWriteS16(r, block) == 0, "wrote an invalid encoding");
    CHECK(geode_pcm_ring_written(r) == 0, "written %lld", static_cast<long long>(geode_pcm_ring_written(r)));
    geode_pcm_ring_destroy(r);
}

// A flush's format is the format of the writes after it; a second flush changes it, and the epoch restarts the count.
void testFlushSetsFormatAndStartsEpoch() {
    geode_pcm_ring* r = makeRing();
    geode_pcm_cursor* cursor = geode_pcm_cursor_create(r, 0);
    geode_pcm_ring_tap_flush(r, 44100, 2, kEncodingS16);
    CHECK(tapWriteS16(r, constantS16(300, 16384)) == 300, "the s16 block was refused");
    CHECK(geode_pcm_ring_written(r) == 300, "written %lld", static_cast<long long>(geode_pcm_ring_written(r)));

    std::vector<float> out(static_cast<size_t>(geode_pcm_cursor_max_frames(cursor)) * 2);
    int restarted = 0;
    int rate = 0;
    int frames = geode_pcm_cursor_take(cursor, out.data(), geode_pcm_cursor_max_frames(cursor), &restarted, &rate);
    CHECK(frames == 300 && rate == 44100, "took %d at %d Hz", frames, rate);
    CHECK(out[0] == 0.5f && out[1] == 0.5f, "s16 16384 read as %f", static_cast<double>(out[0]));

    // A new stream: an epoch, the new rate, floats.
    geode_pcm_ring_tap_flush(r, 48000, 2, kEncodingFloat);
    CHECK(geode_pcm_ring_written(r) == 0, "the flush started no epoch");
    const std::vector<float> floats(static_cast<size_t>(200) * 2, 0.25f);
    CHECK(geode_pcm_ring_tap_write(r, floats.data(), floats.size() * sizeof(float)) == 200, "the float block was refused");
    frames = geode_pcm_cursor_take(cursor, out.data(), geode_pcm_cursor_max_frames(cursor), &restarted, &rate);
    CHECK(frames == 200 && restarted == 1 && rate == 48000, "took %d at %d Hz, restarted %d", frames, rate, restarted);
    CHECK(out[0] == 0.25f, "float read as %f", static_cast<double>(out[0]));
    geode_pcm_cursor_destroy(cursor);
    geode_pcm_ring_destroy(r);
}

// A trailing part of a frame is ignored, as write_bytes does.
void testPartialFrameDropped() {
    geode_pcm_ring* r = makeRing();
    geode_pcm_ring_tap_flush(r, 48000, 2, kEncodingS16);
    const std::vector<int16_t> block = constantS16(10, 100);
    CHECK(geode_pcm_ring_tap_write(r, block.data(), block.size() * sizeof(int16_t) - 2) == 9, "a part frame was written");
    geode_pcm_ring_destroy(r);
}

// The gate: closed, the tap writes nothing and a flush starts no epoch (another source's stream is not cut);
// open again, it writes in the format it was last flushed with.
void testGate() {
    geode_pcm_ring* r = makeRing();
    CHECK(r->tapOwns.load(), "the gate is not open by default");
    geode_pcm_ring_tap_flush(r, 48000, 2, kEncodingS16);
    CHECK(tapWriteS16(r, constantS16(100, 1000)) == 100, "an open gate wrote nothing");

    geode_pcm_ring_tap_set_owner(r, 0);
    CHECK(tapWriteS16(r, constantS16(100, 1000)) == 0, "a closed gate wrote");
    geode_pcm_ring_tap_flush(r, 44100, 2, kEncodingS16);
    CHECK(geode_pcm_ring_written(r) == 100, "a flush with the gate closed started an epoch (%lld)",
          static_cast<long long>(geode_pcm_ring_written(r)));

    geode_pcm_ring_tap_set_owner(r, 1);
    CHECK(tapWriteS16(r, constantS16(50, 1000)) == 50, "the reopened gate wrote nothing");
    geode_pcm_ring_destroy(r);
}

void testNull() {
    geode_pcm_ring_tap_set_owner(nullptr, 1);
    geode_pcm_ring_tap_flush(nullptr, 48000, 2, kEncodingS16);
    const int16_t sample[2] = {0, 0};
    CHECK(geode_pcm_ring_tap_write(nullptr, sample, sizeof(sample)) == 0, "a NULL ring wrote");
}

}  // namespace

int main() {
    testNoFormatNoWrite();
    testFlushSetsFormatAndStartsEpoch();
    testPartialFrameDropped();
    testGate();
    testNull();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("pcm_tap_test: ok");
    return 0;
}
