#include <memory>

#include "api/geode_api.h"
#include "audio/ingest/Aiff.hpp"

// The AIFF reader's C ABI (geode_api.h). Pure: core/tests/aiff_test.cpp links it on the host.

struct geode_aiff {
    geode::audio::ingest::AiffReader reader;
};

extern "C" {

geode_aiff* geode_aiff_create(void) { return new geode_aiff(); }

void geode_aiff_destroy(geode_aiff* a) { std::unique_ptr<geode_aiff> owned(a); }

int geode_aiff_write(geode_aiff* a, const void* bytes, size_t count) {
    if (!a) return -1;
    return static_cast<int>(a->reader.write(static_cast<const uint8_t*>(bytes), count));
}

int geode_aiff_read_s16(geode_aiff* a, int16_t* out, int capacity) {
    return a ? a->reader.read(out, capacity) : -1;
}

int geode_aiff_get_format(const geode_aiff* a, geode_aiff_format* out) {
    if (!a || !out) return 0;
    const auto& r = a->reader;
    *out = {r.channels(), r.sampleRate(), r.bits(), r.totalFrames()};
    return r.state() == geode::audio::ingest::AiffReader::State::kReady ? 1 : 0;
}

float geode_aiff_progress(const geode_aiff* a) { return a ? a->reader.progress() : 0.0f; }

}  // extern "C"
