// Live input's C ABI (audio/ingest/MicInput over audio/ingest/android/OboeMic): one microphone for the process.
// Android only; marshalled by geode_ingest_jni.cpp.
#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"
#include "audio/ingest/MicInput.hpp"
#include "audio/ingest/android/OboeMic.hpp"

namespace {

geode::audio::ingest::MicInput& mic() {
    static geode::audio::ingest::MicInput input(geode::audio::ingest::oboeMicStreams());
    return input;
}

}  // namespace

extern "C" {

int geode_mic_start(int device_rate_hz, int unprocessed) {
    const geode_pcm_ring* ring = geode_pcm_ring_shared(GEODE_PCM_RING_MIC);
    if (!ring) return 0;
    return mic().start(ring->ring, device_rate_hz, unprocessed != 0) ? 1 : 0;
}

void geode_mic_stop(void) { mic().stop(); }

int geode_mic_active(void) { return mic().active() ? 1 : 0; }

}  // extern "C"
