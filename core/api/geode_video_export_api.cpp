// The C ABI over a video export (geode_video_export_api.h): what the shell hands over is gathered into the job's
// request (output/VideoExport) and the phone host's setup (output/android/VideoExportHost), and run() wires the
// two. The plan, the frame loop and the writer are the job's, host-tested in core/tests/video_export_test.cpp;
// nothing is decided here, and no exception leaves it.
#include "api/geode_video_export_api.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "api/AacJobTrack.hpp"
#include "output/VideoExport.hpp"
#include "output/android/VideoExportHost.hpp"

namespace ve = geode::output::videoexport;

struct geode_video_export {
    ~geode_video_export() {
        if (setup.trackFd >= 0) ::close(setup.trackFd);
        if (setup.audioFd >= 0) ::close(setup.audioFd);
    }

    ve::Request request;
    ve::Setup setup;
    std::unique_ptr<ve::Host> host;
    // Guards `job` between the run that makes it and the polls that read it; the job's own state is atomic.
    std::mutex lock;
    std::unique_ptr<ve::Job> job;
    bool cancelRequested = false;
    // run() could not start the job, or the job threw: reported as a failure with no text.
    std::atomic<bool> broken{false};
};

static_assert(static_cast<int>(ve::State::Idle) == GEODE_VIDEO_EXPORT_IDLE);
static_assert(static_cast<int>(ve::State::Running) == GEODE_VIDEO_EXPORT_RUNNING);
static_assert(static_cast<int>(ve::State::Rendered) == GEODE_VIDEO_EXPORT_RENDERED);
static_assert(static_cast<int>(ve::State::Cancelled) == GEODE_VIDEO_EXPORT_CANCELLED);
static_assert(static_cast<int>(ve::State::Failed) == GEODE_VIDEO_EXPORT_FAILED);
static_assert(static_cast<int>(ve::Failure::None) == GEODE_VIDEO_FAILURE_NONE);
static_assert(static_cast<int>(ve::Failure::EncoderRejected) == GEODE_VIDEO_FAILURE_ENCODER_REJECTED);
static_assert(static_cast<int>(ve::Failure::NoOutput) == GEODE_VIDEO_FAILURE_NO_OUTPUT);
static_assert(static_cast<int>(ve::Failure::Stalled) == GEODE_VIDEO_FAILURE_STALLED);
static_assert(static_cast<int>(ve::Failure::EncoderFailed) == GEODE_VIDEO_FAILURE_ENCODER_FAILED);
static_assert(static_cast<int>(ve::Failure::EmptyBuffer) == GEODE_VIDEO_FAILURE_EMPTY_BUFFER);
static_assert(static_cast<int>(ve::Failure::Stopped) == GEODE_VIDEO_FAILURE_STOPPED);

namespace {

// The job once run() has made it, else null.
ve::Job* jobOf(geode_video_export* h) {
    if (!h) return nullptr;
    std::lock_guard<std::mutex> lock(h->lock);
    return h->job.get();
}

void assign(std::vector<float>& into, const float* values, int count) {
    if (values && count > 0) {
        into.assign(values, values + count);
    } else {
        into.clear();
    }
}

}  // namespace

extern "C" {

geode_video_export* geode_video_export_create(void) {
    try {
        return new geode_video_export();
    } catch (...) {
        return nullptr;
    }
}

void geode_video_export_destroy(geode_video_export* h) { delete h; }

void geode_video_export_set_output(geode_video_export* h, int output_fd, const char* cache_dir, int width, int height,
                                   int bit_rate, int fps) {
    if (!h) return;
    try {
        h->setup.outputFd = output_fd;
        h->setup.cacheDir = cache_dir ? cache_dir : "";
        h->setup.width = width;
        h->setup.height = height;
        h->request.width = width;
        h->request.height = height;
        h->request.bitRate = bit_rate;
        h->request.fps = fps;
    } catch (...) {
    }
}

void geode_video_export_add_codec(geode_video_export* h, const char* mime, int top_fps) {
    if (!h || !mime || h->request.codecCount >= geode::output::kAttemptMaxCodecs) return;
    try {
        h->request.mimes[h->request.codecCount] = mime;
        h->request.topFps[h->request.codecCount] = top_fps;
        ++h->request.codecCount;
    } catch (...) {
    }
}

void geode_video_export_set_track(geode_video_export* h, int fd, int64_t offset, int64_t length,
                                  int64_t range_start_ms) {
    if (!h) {
        if (fd >= 0) ::close(fd);
        return;
    }
    if (h->setup.trackFd >= 0) ::close(h->setup.trackFd);
    h->setup.trackFd = fd;
    h->setup.trackOffset = offset;
    h->setup.trackLength = length;
    h->setup.rangeStartMs = range_start_ms;
}

void geode_video_export_set_scene(geode_video_export* h, AAssetManager* assets,
                                  const char* scene_id, int entitled) {
    if (!h) return;
    try {
        h->setup.assets = assets;
        h->setup.sceneId = scene_id ? scene_id : "";
        h->setup.entitled = entitled != 0;
    } catch (...) {
    }
}

void geode_video_export_set_look(geode_video_export* h, const geode_viz_look* look) {
    if (!h || !look) return;
    try {
        ve::Setup& s = h->setup;
        assign(s.params, look->params, look->param_count);
        s.lfos = look->lfos ? look->lfos : "";
        s.adsrs = look->adsrs ? look->adsrs : "";
        assign(s.soundTuning, look->tuning, look->tuning_count);
        s.hasReactions = look->reactions != nullptr;
        s.reactions = look->reactions ? look->reactions : "";
        s.reducedMotion = look->reduced_motion != 0;
        s.transitionId = look->transition_id ? look->transition_id : "";
        s.transitionMs = look->transition_ms;
        s.lookSet = true;
    } catch (...) {
    }
}

void geode_video_export_set_underlay(geode_video_export* h, const uint32_t* pixels, int count, int width, int height,
                                     int blend, float amount) {
    if (!h) return;
    try {
        h->setup.underlayBlend = blend;
        h->setup.underlayAmount = amount;
        const int64_t needed = static_cast<int64_t>(width) * static_cast<int64_t>(height);
        const bool whole = pixels && width > 0 && height > 0 && needed <= count;
        h->setup.underlay.clear();
        if (whole) h->setup.underlay.assign(pixels, pixels + needed);
        h->setup.underlayWidth = whole ? width : 0;
        h->setup.underlayHeight = whole ? height : 0;
    } catch (...) {
    }
}

void geode_video_export_set_mark(geode_video_export* h, const uint8_t* rgba, int tile_w, int tile_h, int placement,
                                 int corner) {
    if (!h) return;
    try {
        const bool marked = rgba && tile_w > 0 && tile_h > 0 && placement != 0;
        h->setup.markRgba.clear();
        if (marked) h->setup.markRgba.assign(rgba, rgba + static_cast<int64_t>(tile_w) * tile_h * 4);
        h->setup.markWidth = marked ? tile_w : 0;
        h->setup.markHeight = marked ? tile_h : 0;
        h->setup.markPlacement = marked ? placement : 0;
        h->setup.markCorner = corner;
    } catch (...) {
    }
}

int geode_video_export_set_audio(geode_video_export* h, const geode_aac_job* job, const char* scratch_path) {
    if (!h || !job || !scratch_path) return 0;
    try {
        geode::api::AacTrack track;
        if (!geode::api::readAacTrack(job, &track)) return 0;
        if (h->setup.audioFd >= 0) ::close(h->setup.audioFd);
        h->setup.audioFd = ::open(scratch_path, O_RDONLY | O_CLOEXEC);
        if (h->setup.audioFd < 0) return 0;
        struct stat info {};
        h->request.audioBytes = ::fstat(h->setup.audioFd, &info) == 0 ? static_cast<int64_t>(info.st_size) : 0;
        h->request.audio = std::move(track.samples);
        h->request.audioFormat = std::move(track.format);
        return 1;
    } catch (...) {
        return 0;
    }
}

void geode_video_export_run(geode_video_export* h) {
    if (!h) return;
    ve::Job* job = nullptr;
    try {
        std::lock_guard<std::mutex> lock(h->lock);
        if (h->job) return;
        h->host = ve::androidHost(h->setup);
        h->job = std::make_unique<ve::Job>(h->request, *h->host);
        if (h->cancelRequested) h->job->cancel();
        job = h->job.get();
    } catch (...) {
        h->broken.store(true, std::memory_order_release);
        return;
    }
    try {
        job->run();
    } catch (...) {
        // Out of memory mid-render: the job closed its scene, encoder and surface as it unwound; the shell
        // discards the file.
        h->broken.store(true, std::memory_order_release);
    }
}

void geode_video_export_cancel(geode_video_export* h) {
    if (!h) return;
    std::lock_guard<std::mutex> lock(h->lock);
    h->cancelRequested = true;
    if (h->job) h->job->cancel();
}

float geode_video_export_progress(geode_video_export* h) {
    const ve::Job* job = jobOf(h);
    return job ? job->progress() : ve::leadInProgress(1.0f);
}

float geode_video_export_lead_in(float done) { return ve::leadInProgress(done); }

int geode_video_export_state(geode_video_export* h) {
    if (!h || h->broken.load(std::memory_order_acquire)) return GEODE_VIDEO_EXPORT_FAILED;
    const ve::Job* job = jobOf(h);
    return job ? static_cast<int>(job->state()) : GEODE_VIDEO_EXPORT_IDLE;
}

int geode_video_export_failure(geode_video_export* h) {
    if (!h || h->broken.load(std::memory_order_acquire)) return GEODE_VIDEO_FAILURE_STOPPED;
    const ve::Job* job = jobOf(h);
    return job ? static_cast<int>(job->failure()) : GEODE_VIDEO_FAILURE_NONE;
}

const char* geode_video_export_detail(geode_video_export* h) {
    if (!h || h->broken.load(std::memory_order_acquire)) return "";
    const ve::Job* job = jobOf(h);
    return job ? job->detail().c_str() : "";
}

}  // extern "C"
