#pragma once

#include <memory>

#include "audio/ingest/PcmRing.hpp"

// The ring behind a geode_pcm_ring handle, for the API files that read one (geode_ingest_api.cpp,
// geode_viz_api.cpp). The handle shares the ring with every cursor made from it, so destroying the
// handle while a renderer or a cursor still reads only drops the writer's side.
struct geode_pcm_ring {
    std::shared_ptr<geode::audio::ingest::PcmRing> ring;
};
