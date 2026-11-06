#include <memory>

#include "api/geode_api.h"
#include "audio/ingest/FileFeed.hpp"

// The export file ring's C ABI (geode_api.h). Pure: no GL, so core/tests/file_feed_test.cpp links it on
// the host. geode_viz_feed_file, which hands a frame to a renderer, is in geode_viz_api.cpp.

extern "C" {

geode_file_feed* geode_file_feed_create(long long start_us) {
    return new geode_file_feed(start_us);
}

void geode_file_feed_destroy(geode_file_feed* f) {
    std::unique_ptr<geode_file_feed> owned(f);
}

long long geode_file_feed_write(geode_file_feed* f, const void* bytes, size_t byte_count, int encoding,
                                int sample_rate, int channels, long long pts_us) {
    if (!f) return -1;
    return f->feed.write(bytes, byte_count, encoding, sample_rate, channels, pts_us);
}

int geode_file_feed_needs(const geode_file_feed* f, long long frame_index, int fps) {
    return f && f->feed.needs(frame_index, fps) ? 1 : 0;
}

}  // extern "C"
