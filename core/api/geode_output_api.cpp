// The export geometry ABI (core/output/ExportGeometry): thin C wrappers, no logic of their own.
#include "api/geode_api.h"
#include "output/ExportGeometry.hpp"

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

}  // extern "C"
