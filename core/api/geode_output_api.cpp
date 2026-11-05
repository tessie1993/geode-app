// The export output ABI (core/output/ExportGeometry, core/output/MuxerBudget): thin C wrappers, no logic of
// their own.
#include "api/geode_api.h"
#include "output/ExportGeometry.hpp"
#include "output/MuxerBudget.hpp"

extern "C" {

int geode_export_size(int short_side, int w_ratio, int h_ratio, int* out_width, int* out_height) {
    const geode::output::ExportSize s = geode::output::exportSize(short_side, w_ratio, h_ratio);
    if (out_width) *out_width = s.width;
    if (out_height) *out_height = s.height;
    return s.width > 0 ? 1 : 0;
}

int geode_export_fps(int requested_fps) { return geode::output::exportFps(requested_fps); }

int geode_export_total_frames(int64_t duration_us, int fps) {
    return geode::output::exportTotalFrames(duration_us, fps);
}

int64_t geode_export_frame_duration_ns(int fps) { return geode::output::exportFrameDurationNs(fps); }

int geode_muxer_capped_video_bps(int requested_bps, int64_t duration_us, int64_t audio_bytes) {
    return geode::output::cappedVideoBps(requested_bps, duration_us, audio_bytes);
}

int64_t geode_muxer_aac_bytes(int64_t duration_us) { return geode::output::audioBytes(duration_us); }

}  // extern "C"
