#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "audio/ingest/PcmRing.hpp"

struct geode_playout;

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

    std::shared_ptr<const geode::audio::ingest::PcmRing> layerRing() const {
        std::lock_guard<std::mutex> lock(layerLock);
        return layer;
    }
};
