#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>

#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"
#include "output/CastAudioPump.hpp"

struct geode_cast_audio {
    const geode_pcm_ring* music;  // compared with each pump's ring only; the pump holds the ring itself
    geode::output::CastAudioPump pump;
};

// No exception may leave an extern "C" function: create and pump allocate (once, or on a rate change) and
// report nothing made or nothing to queue instead.
extern "C" {

geode_cast_audio* geode_cast_audio_create(const geode_pcm_ring* music, int out_rate_hz, int channels) {
    if (out_rate_hz <= 0 || channels <= 0) return nullptr;
    try {
        std::shared_ptr<const geode::audio::ingest::PcmRing> ring = music ? music->ring : nullptr;
        return new geode_cast_audio{music, geode::output::CastAudioPump(std::move(ring), out_rate_hz, channels)};
    } catch (...) {
        return nullptr;
    }
}

void geode_cast_audio_destroy(geode_cast_audio* a) {
    std::unique_ptr<geode_cast_audio> owned(a);
}

int geode_cast_audio_pump(geode_cast_audio* a, const geode_pcm_ring* current, int64_t until_us) {
    if (!a) return 0;
    try {
        return a->pump.pump(current != nullptr && current == a->music, until_us);
    } catch (...) {
        return 0;
    }
}

int geode_cast_audio_read(const geode_cast_audio* a, int16_t* out, int capacity) {
    if (!a || !out || capacity < 0) return 0;
    const int frames = a->pump.blockFrames();
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(a->pump.channels());
    if (samples > static_cast<size_t>(capacity)) return 0;
    std::copy(a->pump.block(), a->pump.block() + samples, out);
    return frames;
}

}  // extern "C"
