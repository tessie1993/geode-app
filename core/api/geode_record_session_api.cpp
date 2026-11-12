// geode_record_session_* (core/api/geode_api.h): a recording's encoders and file, output/RecordSession on the
// phone's codecs (output/android/MediaCodecSink). Android only: the host test drives RecordSession with fakes
// (core/tests/encode_sink_test.cpp), and geode_record_api.cpp, which the recorder's host test builds, stays
// free of the NDK media library.
#include <memory>

#include "api/geode_api.h"
#include "api/geode_record_handles.hpp"
#include "output/RecordSession.hpp"
#include "output/android/MediaCodecSink.hpp"

using geode::output::record::Session;

// The session and a recorder handle on the same clock, the form geode_viz_record_start takes it in.
struct geode_record_session {
    std::unique_ptr<Session> session;
    geode_recorder recorder;
};

static_assert(static_cast<int>(Session::State::Running) == GEODE_RECORD_SESSION_RUNNING);
static_assert(static_cast<int>(Session::State::Closing) == GEODE_RECORD_SESSION_CLOSING);
static_assert(static_cast<int>(Session::State::Saved) == GEODE_RECORD_SESSION_SAVED);
static_assert(static_cast<int>(Session::State::Failed) == GEODE_RECORD_SESSION_FAILED);

extern "C" {

geode_record_session* geode_record_session_open(const geode_recorder* recorder, int fd, int stage_w, int stage_h,
                                                int short_side, int fps) {
    if (!recorder || !recorder->recorder || fd < 0) return nullptr;
    try {
        auto h = std::make_unique<geode_record_session>();
        h->recorder.recorder = recorder->recorder;
        h->session = Session::open(recorder->recorder, geode::output::sink::mediaCodecs(fd), stage_w, stage_h,
                                   short_side, fps, [] { return geode_record_now_ns(); });
        if (!h->session) return nullptr;
        return h.release();
    } catch (...) {
        return nullptr;
    }
}

void geode_record_session_destroy(geode_record_session* h) { delete h; }

void geode_record_session_size(const geode_record_session* h, int* out_w, int* out_h) {
    if (out_w) *out_w = h ? h->session->width() : 0;
    if (out_h) *out_h = h ? h->session->height() : 0;
}

int geode_record_session_state(const geode_record_session* h) {
    return h ? static_cast<int>(h->session->state()) : GEODE_RECORD_SESSION_FAILED;
}

void geode_record_session_tee_closed(geode_record_session* h) {
    if (h) h->session->teeClosed();
}

int geode_record_session_tee_start(geode_record_session* h, geode_viz* v, geode_export_pass* watermark) {
    if (!h || !v) return 0;
    try {
        const int width = h->session->width();
        const int height = h->session->height();
        const bool started = h->session->withInputWindow([&](void* window) {
            return geode_viz_record_start(v, &h->recorder, window, width, height, watermark) != 0;
        });
        return started ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

}  // extern "C"
