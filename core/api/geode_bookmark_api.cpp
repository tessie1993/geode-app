#include "api/geode_api.h"
#include "audio/dsp/TrackBookmark.hpp"

extern "C" {

int geode_bookmark_verdict(int64_t position_ms, int64_t duration_ms) {
    return static_cast<int>(geode::audio::bookmarkVerdict(position_ms, duration_ms));
}

int geode_bookmark_resumes_from(int64_t current_position_ms) {
    return geode::audio::bookmarkResumesFrom(current_position_ms) ? 1 : 0;
}

}  // extern "C"
