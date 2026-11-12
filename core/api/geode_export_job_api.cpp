// The C ABI over an export's render (geode_export_job_api.h): the renderer as output::ExportTarget through the
// geode_viz calls, and the track opened natively from the descriptor the shell hands over. The order and the
// pump are core/output/ExportFrame and core/audio/ingest/TrackPump, host-tested; this only wires them, and no
// exception leaves it.
#include "api/geode_export_job_api.h"

#include <unistd.h>

#include <memory>
#include <string>
#include <utility>

#include "audio/ingest/TrackPump.hpp"
#include "audio/ingest/android/TrackDecoder.hpp"
#include "output/ExportFrame.hpp"

namespace {

using geode::audio::ingest::TrackFeed;
using geode::audio::ingest::TrackSpan;

class VizTarget final : public geode::output::ExportTarget {
public:
    explicit VizTarget(geode_viz* viz) : viz_(viz) {}

    bool hear(TrackFeed& feed) override {
        const TrackFeed::Sink sink = [](void* context, const float* interleaved, int frames, int channels,
                                        int sampleRate) {
            geode_viz_write_audio(static_cast<geode_viz*>(context), interleaved, frames, channels, sampleRate);
        };
        return feed.deliver(sink, viz_);
    }
    void prepareOffscreen() override { geode_viz_prepare_offscreen(viz_); }
    void setFrameRate(int fps) override { geode_viz_set_frame_rate(viz_, fps); }
    void surface(int width, int height) override {
        geode_viz_surface_created(viz_);
        geode_viz_surface_changed(viz_, width, height);
    }
    bool setScene(const std::string& sceneId) override { return geode_viz_set_scene(viz_, sceneId.c_str()) != 0; }
    void loadMilkPreset(const std::string& path) override { geode_viz_load_milk_preset(viz_, path.c_str()); }
    uint32_t pinEngineSettings(bool entitled, int64_t durationMs) override {
        return geode_viz_pin_engine_settings(viz_, entitled ? 1 : 0, static_cast<long long>(durationMs));
    }
    bool endPreRoll(int64_t rangeStartMs) override { return geode_viz_end_pre_roll(viz_, rangeStartMs) != 0; }
    void render(double timeSeconds, uint32_t targetFbo) override { geode_viz_render(viz_, timeSeconds, targetFbo); }

private:
    geode_viz* viz_;
};

}  // namespace

struct geode_export_job {
    geode_export_job(geode_viz* viz, TrackSpan track, int fps, int64_t rangeStartMs)
        : target(viz), span(track), frames(fps, rangeStartMs, [this](int64_t startMs) {
              return geode::audio::ingest::openTrack(span, startMs, geode::audio::ingest::openPlatformDecoder);
          }) {}
    ~geode_export_job() {
        if (span.fd >= 0) ::close(span.fd);
    }
    geode_export_job(const geode_export_job&) = delete;
    geode_export_job& operator=(const geode_export_job&) = delete;

    VizTarget target;
    TrackSpan span;  // the descriptor the job owns; each decode reads a duplicate
    geode::output::ExportFrames frames;
};

extern "C" {

geode_export_job* geode_export_job_create(geode_viz* viz, int fd, int64_t offset, int64_t length, int fps,
                                          int64_t range_start_ms) {
    if (!viz || fd < 0 || fps <= 0) {
        if (fd >= 0) ::close(fd);
        return nullptr;
    }
    try {
        return new geode_export_job(viz, TrackSpan{fd, offset, length}, fps, range_start_ms);
    } catch (...) {
        ::close(fd);
        return nullptr;
    }
}

void geode_export_job_destroy(geode_export_job* job) {
    std::unique_ptr<geode_export_job> owned(job);
}

int64_t geode_export_job_begin(geode_export_job* job, int width, int height, const char* scene_id,
                               const char* milk_path, int entitled, int64_t pin_duration_ms) {
    if (!job || !scene_id) return -1;
    try {
        geode::output::ExportStart start;
        start.width = width;
        start.height = height;
        start.sceneId = scene_id;
        start.milkPresetPath = milk_path ? milk_path : "";
        start.entitled = entitled != 0;
        start.pinDurationMs = pin_duration_ms;
        return job->frames.begin(start, job->target);
    } catch (...) {
        return -1;
    }
}

int geode_export_job_warm(geode_export_job* job) {
    if (!job) return 0;
    try {
        return job->frames.preRoll(job->target) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int geode_export_job_frame(geode_export_job* job, int64_t frame, uint32_t target_fbo) {
    if (!job) return 0;
    try {
        job->frames.frame(frame, target_fbo, job->target);
        return 1;
    } catch (...) {
        return 0;
    }
}

}  // extern "C"
