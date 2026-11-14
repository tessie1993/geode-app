// Host test for core/audio/ingest/RingKinds and its C ABI (geode_pcm_ring_shared, geode_pcm_ring_create_kind):
// the rings the app keeps, which the Kotlin SampleRing used to size and free.
//
// The player's and the mic's rings are the process's: the same handle on every ask and from every thread, sized as
// SampleRing made them, the mic layered over the player, and never freed (destroy leaves them alone). The output
// mix's is one per owner: stitched snapshots, freed by its owner while a reader's cursor still holds the ring.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore -Icore/tests/stub core/tests/pcm_ring_kinds_test.cpp
//   core/audio/ingest/PcmRing.cpp core/audio/ingest/AiffReader.cpp core/audio/ingest/TrackFeed.cpp
//   core/audio/ingest/SnapshotStitch.cpp core/api/geode_ingest_api.cpp core/output/FrameClock.cpp
//   -o pcm_ring_kinds_test && ./pcm_ring_kinds_test
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"
#include "audio/ingest/RingKinds.hpp"

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

constexpr int kRate = 44100;

std::vector<float> stereo(int frames) {
    std::vector<float> s(static_cast<size_t>(frames) * 2);
    for (int f = 0; f < frames; ++f) {
        s[static_cast<size_t>(f) * 2] = 0.25f;
        s[static_cast<size_t>(f) * 2 + 1] = -0.25f;
    }
    return s;
}

// The sizes SampleRing gave each ring: the player's from the playout constants, the mic's 2^15 mono and the
// output mix's 32768 stereo, each with a quarter of the ring as runway unless given.
void testSizes() {
    geode_pcm_ring* player = geode_pcm_ring_shared(GEODE_PCM_RING_PLAYER);
    geode_pcm_ring* mic = geode_pcm_ring_shared(GEODE_PCM_RING_MIC);
    CHECK(player && mic, "no process rings");
    if (!player || !mic) return;
    CHECK(player->ring->capacityFrames() == (1 << 19) && player->ring->channelCount() == 2, "player ring size");
    CHECK(geode_pcm_ring_max_write_frames(player) == (1 << 14), "player runway %d", geode_pcm_ring_max_write_frames(player));
    CHECK(mic->ring->capacityFrames() == (1 << 15) && mic->ring->channelCount() == 1, "mic ring size");
    CHECK(geode_pcm_ring_max_write_frames(mic) == (1 << 15) / 4, "mic runway %d", geode_pcm_ring_max_write_frames(mic));

    geode_pcm_ring* mix = geode_pcm_ring_create_kind(GEODE_PCM_RING_OUTPUT_MIX);
    CHECK(mix != nullptr, "no output mix ring");
    if (!mix) return;
    CHECK(mix->ring->capacityFrames() == 32768 && mix->ring->channelCount() == 2, "output mix ring size");
    CHECK(geode_pcm_ring_max_write_frames(mix) == 8192, "output mix runway %d", geode_pcm_ring_max_write_frames(mix));
    CHECK(mix->snapshots != nullptr, "the output mix's writes are not stitched");
    CHECK(player->snapshots == nullptr && mic->snapshots == nullptr, "a stream ring is stitched");
    geode_pcm_ring_destroy(mix);

    for (int kind = 0; kind < geode::audio::ingest::kRingKinds; ++kind) {
        const geode::audio::ingest::RingSpec& s = geode::audio::ingest::kRingSpecs[kind];
        CHECK(geode::audio::ingest::PcmRing::valid(s.capacityFrames, s.channels, s.maxWriteFrames), "kind %d is no ring", kind);
    }
    CHECK(geode_pcm_ring_max_write_frames(nullptr) == 0, "a null ring has a runway");
}

// One ring per kind for the process: every ask, from any thread, gets the same handle; other kinds get none.
void testShared() {
    geode_pcm_ring* player = geode_pcm_ring_shared(GEODE_PCM_RING_PLAYER);
    geode_pcm_ring* mic = geode_pcm_ring_shared(GEODE_PCM_RING_MIC);
    CHECK(player != mic, "the player and the mic share a ring");
    CHECK(geode_pcm_ring_shared(GEODE_PCM_RING_PLAYER) == player, "a second ask made another player ring");
    CHECK(geode_pcm_ring_shared(GEODE_PCM_RING_OUTPUT_MIX) == nullptr, "the output mix is kept for the process");
    CHECK(geode_pcm_ring_shared(-1) == nullptr && geode_pcm_ring_shared(3) == nullptr, "an unknown kind has a ring");
    CHECK(geode_pcm_ring_create_kind(-1) == nullptr && geode_pcm_ring_create_kind(3) == nullptr, "made an unknown kind");

    std::vector<geode_pcm_ring*> seen(8, nullptr);
    std::vector<std::thread> threads;
    for (size_t i = 0; i < seen.size(); ++i) {
        threads.emplace_back([&seen, i] { seen[i] = geode_pcm_ring_shared(i % 2 ? GEODE_PCM_RING_MIC : GEODE_PCM_RING_PLAYER); });
    }
    for (std::thread& t : threads) t.join();
    for (size_t i = 0; i < seen.size(); ++i) {
        CHECK(seen[i] == (i % 2 ? mic : player), "thread %zu got another ring", i);
    }

    // The mic is layered over the player's ring, so every renderer fed the player hears the room too.
    CHECK(player->layerRing() == mic->ring, "the mic is not layered over the player");
    CHECK(mic->layerRing() == nullptr, "the mic has a layer");
}

// destroy leaves a process ring alone: a writer, a stage or a renderer that still holds its handle keeps working.
void testProcessRingsOutliveDestroy() {
    geode_pcm_ring* player = geode_pcm_ring_shared(GEODE_PCM_RING_PLAYER);
    geode_pcm_cursor* cursor = geode_pcm_cursor_create(player, 0);
    geode_pcm_ring_destroy(player);
    CHECK(geode_pcm_ring_shared(GEODE_PCM_RING_PLAYER) == player, "destroy freed the player's ring");

    // A new session's stream: an epoch, then the tap's writes, as the session before it.
    geode_pcm_ring_begin_epoch(player);
    CHECK(geode_pcm_ring_written(player) == 0, "an epoch did not restart the count");
    const std::vector<float> block = stereo(480);
    float peak = 0.0f;
    CHECK(geode_pcm_ring_write_f32(player, block.data(), 480, 2, kRate, &peak) == 480, "the write after destroy was refused");
    CHECK(peak == 0.25f, "peak %f", static_cast<double>(peak));
    CHECK(geode_pcm_ring_written(player) == 480, "written %lld", static_cast<long long>(geode_pcm_ring_written(player)));

    std::vector<float> out(static_cast<size_t>(geode_pcm_cursor_max_frames(cursor)) * 2);
    int restarted = 0;
    int rate = 0;
    const int frames = geode_pcm_cursor_take(cursor, out.data(), geode_pcm_cursor_max_frames(cursor), &restarted, &rate);
    CHECK(frames == 480 && restarted == 1 && rate == kRate, "took %d at %d Hz, restarted %d", frames, rate, restarted);
    geode_pcm_cursor_destroy(cursor);
}

// The output mix's ring is its owner's: freed by destroy, while a reader's cursor keeps the samples it reads.
void testOwnedRingReaderOutlivesOwner() {
    geode_pcm_ring* mix = geode_pcm_ring_create_kind(GEODE_PCM_RING_OUTPUT_MIX);
    CHECK(mix != nullptr && !mix->kept, "the output mix ring is kept");
    if (!mix) return;
    geode_pcm_cursor* cursor = geode_pcm_cursor_create(mix, 0);
    geode_pcm_ring_begin_epoch(mix);
    std::vector<uint8_t> snapshot(1024);
    for (size_t i = 0; i < snapshot.size(); ++i) snapshot[i] = static_cast<uint8_t>(i % 2 ? 160 : 96);
    const int wrote = geode_pcm_ring_write_bytes(mix, snapshot.data(), snapshot.size(), GEODE_PCM_U8, 1, 48000, nullptr);
    CHECK(wrote > 0, "the snapshot was refused");
    geode_pcm_ring_destroy(mix);

    std::vector<float> out(static_cast<size_t>(geode_pcm_cursor_max_frames(cursor)) * 2);
    int restarted = 0;
    int rate = 0;
    const int frames = geode_pcm_cursor_take(cursor, out.data(), geode_pcm_cursor_max_frames(cursor), &restarted, &rate);
    CHECK(frames > 0 && rate == 48000, "the reader lost the ring with its owner: %d frames at %d Hz", frames, rate);
    bool heard = false;
    for (int i = 0; i < frames * 2; ++i) heard = heard || out[static_cast<size_t>(i)] != 0.0f;
    CHECK(heard, "the reader read silence");
    geode_pcm_cursor_destroy(cursor);

    // A ring made by kind for the player's size is its owner's too, not the process's.
    geode_pcm_ring* own = geode_pcm_ring_create_kind(GEODE_PCM_RING_PLAYER);
    CHECK(own != nullptr && own != geode_pcm_ring_shared(GEODE_PCM_RING_PLAYER) && !own->kept, "made the process ring");
    geode_pcm_ring_destroy(own);
    geode_pcm_ring_destroy(nullptr);
}

}  // namespace

int main() {
    testSizes();
    testShared();
    testProcessRingsOutliveDestroy();
    testOwnedRingReaderOutlivesOwner();
    if (failures) {
        std::fprintf(stderr, "pcm_ring_kinds_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("pcm_ring_kinds_test: ok\n");
    return EXIT_SUCCESS;
}
