// Host test for a live stage's ring at teardown (core/stage/LiveClient with a real geode_pcm_ring handle): the
// dream's close, where Stage.close clears the stage's sound and destroys it, then the output
// mix's owner destroys the ring (OutputMixCapture.release). Destroy's wait is bounded: a frame slower than it is
// still running, on a thread nobody waits for, when the owner's destroy comes.
//
// What is pinned: a frame in flight keeps the handle it began with until it ends, whatever the owner does meanwhile;
// the frame's end lets it go, so nothing leaks; with no frame in flight the owner's destroy frees it at once.
// The model frame reads the handle as geode_viz_feed_ring does. Run it under ASan too (live_ring_teardown_asan).
#include <cstdio>
#include <future>
#include <memory>
#include <thread>

#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"
#include "stage/LiveClient.hpp"

using namespace geode::stage;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

// As geode_stage_set_sound hands the stage a ring.
void hear(LiveClient& client, const geode_pcm_ring* ring) { client.setSound(geode::api::holdRing(ring)); }

struct Seen {
    std::weak_ptr<const geode::audio::ingest::PcmRing> inner;  // alive exactly while its handle is
    std::promise<void> entered;
    std::shared_future<void> tornDown;
    bool aliveInFrame = false;
    int readInFrame = -1;
};

class Viz final : public LiveViz {
public:
    explicit Viz(Seen& seen) : s_(seen) {}
    void surfaceCreated() override {}
    void surfaceChanged(int, int) override {}
    uint32_t frame(const void* ring, float, float) override {
        if (!ring) return 0;
        s_.entered.set_value();
        s_.tornDown.wait();
        // geode_viz_feed_ring reads the handle itself (its layer, ring, playout and analysis), not only the ring.
        s_.aliveInFrame = !s_.inner.expired();
        s_.readInFrame = geode_pcm_ring_max_write_frames(static_cast<const geode_pcm_ring*>(ring));
        return 0;
    }
    void trimSpares() override {}
    void letSoundGo() override {}
    void releaseScenes() override {}
    void warmTransition(const std::string&) override {}
    bool recordStart(const RecordAsk&) override { return false; }
    void recordFailed(const RecordAsk&) override {}
    void recordStop(const RecordAsk&) override {}
    std::string error() override { return {}; }
    int takeDirectorMoments() override { return 0; }
    int liquidMode() override { return 0; }
    bool readStill(Still&) override { return false; }
    void requestStill() override {}

private:
    Seen& s_;
};

// The close a frame slower than destroy's wait outlives: the owner destroys the ring while that frame still runs.
void slowFrameOutlivesTheOwner() {
    Seen seen;
    std::promise<void> tearDown;
    seen.tornDown = tearDown.get_future().share();
    LiveClient client(std::make_unique<Viz>(seen));
    geode_pcm_ring* ring = geode_pcm_ring_create_kind(GEODE_PCM_RING_OUTPUT_MIX);
    check(ring != nullptr, "an output-mix ring is made");
    if (!ring) return;
    const int runway = geode_pcm_ring_max_write_frames(ring);
    seen.inner = ring->ring;
    hear(client, ring);

    std::thread stage([&] { client.frame(60.0f, 60.0f); });
    seen.entered.get_future().wait();
    // Stage.close, then SoundSource.release: the stage stops hearing it, and the owner frees it.
    hear(client, nullptr);
    geode_pcm_ring_destroy(ring);
    tearDown.set_value();
    stage.join();

    check(seen.aliveInFrame, "the frame in flight still holds the ring its owner destroyed");
    check(seen.readInFrame == runway, "the frame reads the handle it began with, not freed memory");
    check(seen.inner.expired(), "the ring goes as that frame ends: the owner's destroy is not lost");
}

// The usual close: no frame in flight, so the owner's destroy frees the ring there and then.
void idleStageLetsGoAtOnce() {
    Seen seen;
    LiveClient client(std::make_unique<Viz>(seen));
    geode_pcm_ring* ring = geode_pcm_ring_create_kind(GEODE_PCM_RING_OUTPUT_MIX);
    check(ring != nullptr, "an output-mix ring is made");
    if (!ring) return;
    seen.inner = ring->ring;
    hear(client, ring);
    hear(client, nullptr);
    geode_pcm_ring_destroy(ring);
    check(seen.inner.expired(), "a stage that no longer hears the ring does not keep it");
}

}  // namespace

int main() {
    slowFrameOutlivesTheOwner();
    idleStageLetsGoAtOnce();
    if (failures == 0) std::printf("live_ring_teardown_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
