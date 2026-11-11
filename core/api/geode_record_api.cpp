// geode_recorder_* and the Record plan (core/api/geode_api.h): Record's clock, core/output/Recorder.
#include <memory>
#include <new>

#include "api/geode_api.h"
#include "api/geode_record_handles.hpp"

using namespace geode::output;

static_assert(static_cast<int>(record::Stop::User) == GEODE_RECORD_STOP_USER);
static_assert(static_cast<int>(record::Stop::MaxLength) == GEODE_RECORD_STOP_MAX_LENGTH);
static_assert(static_cast<int>(record::Stop::Resized) == GEODE_RECORD_STOP_RESIZED);
static_assert(static_cast<int>(record::Stop::FileFull) == GEODE_RECORD_STOP_FILE_FULL);
static_assert(static_cast<int>(record::Stop::Failed) == GEODE_RECORD_STOP_FAILED);
static_assert(record::Recorder::kAudioRateHz == GEODE_RECORD_AUDIO_RATE_HZ);
static_assert(record::Recorder::kChannels == GEODE_RECORD_AUDIO_CHANNELS);

extern "C" {

geode_recorder* geode_recorder_create(int stage_w, int stage_h, int fps, int max_sec, int64_t cap_ms) {
    if (stage_w <= 0 || stage_h <= 0) return nullptr;
    auto* h = new (std::nothrow) geode_recorder();
    if (!h) return nullptr;
    try {
        h->recorder = std::make_shared<record::Recorder>(record::Config{stage_w, stage_h, fps, max_sec, cap_ms});
    } catch (...) {
        delete h;
        return nullptr;
    }
    return h;
}

void geode_recorder_destroy(geode_recorder* h) { delete h; }

int64_t geode_recorder_frame(geode_recorder* h, int64_t now_ns, int stage_w, int stage_h) {
    return h ? h->recorder->frame(now_ns, stage_w, stage_h) : -1;
}

void geode_recorder_hear(geode_recorder* h, const float* interleaved, int frames, int channels, int sample_rate, int64_t now_ns) {
    if (h) h->recorder->hear(interleaved, frames, channels, sample_rate, now_ns);
}

void geode_recorder_restarted(geode_recorder* h) {
    if (h) h->recorder->restarted();
}

void geode_recorder_video_encoded(geode_recorder* h) {
    if (h) h->recorder->videoEncoded();
}

void geode_recorder_wrote(geode_recorder* h, int64_t bytes, int64_t now_ns) {
    if (h) h->recorder->wrote(bytes, now_ns);
}

int geode_recorder_take_audio(geode_recorder* h, int64_t now_ns, int16_t* out, int capacity_frames, int64_t* out_pts_us) {
    return h ? h->recorder->takeAudio(now_ns, out, capacity_frames, out_pts_us) : 0;
}

void geode_recorder_stop(geode_recorder* h, int why, int64_t now_ns) {
    if (!h || why < GEODE_RECORD_STOP_USER || why > GEODE_RECORD_STOP_FAILED) return;
    h->recorder->stop(static_cast<record::Stop>(why), now_ns);
}

int geode_recorder_stopped(const geode_recorder* h) { return h ? static_cast<int>(h->recorder->stopped()) : 0; }

int64_t geode_recorder_recorded_ms(const geode_recorder* h, int64_t now_ns) { return h ? h->recorder->recordedMs(now_ns) : 0; }

int64_t geode_recorder_frames_dropped(const geode_recorder* h) { return h ? h->recorder->framesDropped() : 0; }

void geode_record_frame_size(int stage_w, int stage_h, int short_side, int* out_w, int* out_h) {
    const encode::FrameSize size = record::frameSize(stage_w, stage_h, short_side);
    if (out_w) *out_w = size.width;
    if (out_h) *out_h = size.height;
}

int geode_record_video_bps(int width, int height, int fps, int64_t limit_ms) {
    return record::videoBps(width, height, fps, limit_ms);
}

}  // extern "C"
