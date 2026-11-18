#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "audio/ingest/PcmRing.hpp"
#include "audio/ingest/SnapshotStitch.hpp"

struct geode_playout;

namespace geode::sound {
class SharedAnalysis;
}  // namespace geode::sound

// The ring behind a geode_pcm_ring handle, for the API files that read one (geode_ingest_api.cpp,
// geode_viz_api.cpp). The handle shares the ring with every cursor made from it, so destroying the
// handle while a renderer or a cursor still reads only drops the writer's side.
struct geode_pcm_ring {
    std::shared_ptr<geode::audio::ingest::PcmRing> ring;
    // geode_pcm_ring_set_playout: the clock of the sink that plays this ring; null reads the newest.
    std::atomic<const geode_playout*> playout{nullptr};
    // geode_pcm_ring_set_layer: a second source every renderer fed this handle hears over it (the mic over
    // the player). Set on the main thread, read once per frame on each render thread.
    mutable std::mutex layerLock{};
    std::shared_ptr<const geode::audio::ingest::PcmRing> layer{};
    // geode_pcm_ring_set_snapshots: the writes are a capture's snapshots, stitched onto the capture clock.
    // Writer thread only, like the writes.
    std::unique_ptr<geode::audio::ingest::SnapshotStitch> snapshots{};
    // geode_pcm_ring_tap_*: the Media3 tap's gate and the format its last flush announced. The tap's thread
    // writes the format and reads the gate; the shell sets the gate from the main thread.
    std::atomic<bool> tapOwns{true};
    std::atomic<int> tapSampleRate{0};
    std::atomic<int> tapChannels{0};
    std::atomic<int> tapEncoding{0};
    // A ring kept for the process (geode_pcm_ring_shared): geode_pcm_ring_destroy leaves it alone.
    bool kept = false;
    // The ring's one analysis (sound/hop/SharedAnalysis), made when the first renderer hears the ring
    // (geode_viz_feed_ring) and shared with every later one. Its lock is taken only when a renderer's ring changes.
    mutable std::mutex analysisLock{};
    mutable std::shared_ptr<geode::sound::SharedAnalysis> analysis{};

    std::shared_ptr<const geode::audio::ingest::PcmRing> layerRing() const {
        std::lock_guard<std::mutex> lock(layerLock);
        return layer;
    }
};
