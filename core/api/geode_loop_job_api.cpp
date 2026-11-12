// The C ABI over a loop render (geode_loop_job_api.h): what the shell hands over is gathered into the job's
// request (output/LoopRender) and the phone host's setup (output/android/LoopRenderHost), and run() wires the
// two. The plan, the frame loop, the seam and the writer are the job's, host-tested in
// core/tests/loop_render_test.cpp; nothing is decided here, and no exception leaves it.
#include "api/geode_loop_job_api.h"

#include <unistd.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "output/LoopRender.hpp"
#include "output/android/LoopRenderHost.hpp"

namespace lr = geode::output::looprender;

struct geode_loop_job {
    ~geode_loop_job() {
        if (setup.trackFd >= 0) ::close(setup.trackFd);
    }

    lr::Request request;
    lr::Setup setup;
    std::unique_ptr<lr::Host> host;
    // Guards `job` between the run that makes it and the polls that read it; the job's own state is atomic.
    std::mutex lock;
    std::unique_ptr<lr::Job> job;
    bool cancelRequested = false;
    // run() could not start the job, or the job threw: reported as a failure with no text.
    std::atomic<bool> broken{false};
};

static_assert(static_cast<int>(lr::State::Idle) == GEODE_LOOP_JOB_IDLE);
static_assert(static_cast<int>(lr::State::Running) == GEODE_LOOP_JOB_RUNNING);
static_assert(static_cast<int>(lr::State::Rendered) == GEODE_LOOP_JOB_RENDERED);
static_assert(static_cast<int>(lr::State::Cancelled) == GEODE_LOOP_JOB_CANCELLED);
static_assert(static_cast<int>(lr::State::Failed) == GEODE_LOOP_JOB_FAILED);
static_assert(static_cast<int>(lr::Failure::None) == GEODE_LOOP_FAILURE_NONE);
static_assert(static_cast<int>(lr::Failure::EncoderRejected) == GEODE_LOOP_FAILURE_ENCODER_REJECTED);
static_assert(static_cast<int>(lr::Failure::ShaderCompile) == GEODE_LOOP_FAILURE_SHADER_COMPILE);
static_assert(static_cast<int>(lr::Failure::LowGpuMemory) == GEODE_LOOP_FAILURE_LOW_GPU_MEMORY);
static_assert(static_cast<int>(lr::Failure::NoOutput) == GEODE_LOOP_FAILURE_NO_OUTPUT);
static_assert(static_cast<int>(lr::Failure::Stalled) == GEODE_LOOP_FAILURE_STALLED);
static_assert(static_cast<int>(lr::Failure::EncoderFailed) == GEODE_LOOP_FAILURE_ENCODER_FAILED);
static_assert(static_cast<int>(lr::Failure::CacheWrite) == GEODE_LOOP_FAILURE_CACHE_WRITE);
static_assert(static_cast<int>(lr::Failure::Stopped) == GEODE_LOOP_FAILURE_STOPPED);

namespace {

// The job once run() has made it, else null.
lr::Job* jobOf(geode_loop_job* h) {
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

geode_loop_job* geode_loop_job_create(void) {
    try {
        return new geode_loop_job();
    } catch (...) {
        return nullptr;
    }
}

void geode_loop_job_destroy(geode_loop_job* h) { delete h; }

void geode_loop_job_set_output(geode_loop_job* h, const char* cache_dir, int width, int height, int bit_rate,
                               int64_t loop_ms, int64_t crossfade_ms, int fps, int64_t long_form_ms) {
    if (!h) return;
    try {
        h->setup.cacheDir = cache_dir ? cache_dir : "";
        h->setup.width = width;
        h->setup.height = height;
        h->request.width = width;
        h->request.height = height;
        h->request.bitRate = bit_rate;
        h->request.loopMs = loop_ms;
        h->request.crossfadeMs = crossfade_ms;
        h->request.fps = fps;
        h->request.longFormMs = long_form_ms;
    } catch (...) {
    }
}

void geode_loop_job_set_drift(geode_loop_job* h, float hue_turns, float warmth, int stops) {
    if (!h) return;
    h->request.hueTurns = hue_turns;
    h->request.warmth = warmth;
    h->request.stops = stops;
}

void geode_loop_job_add_codec(geode_loop_job* h, const char* mime, int top_fps) {
    if (!h || !mime || h->request.codecCount >= geode::output::kAttemptMaxCodecs) return;
    try {
        h->request.mimes[h->request.codecCount] = mime;
        h->request.topFps[h->request.codecCount] = top_fps;
        ++h->request.codecCount;
    } catch (...) {
    }
}

void geode_loop_job_set_track(geode_loop_job* h, int fd, int64_t offset, int64_t length) {
    if (!h) {
        if (fd >= 0) ::close(fd);
        return;
    }
    if (h->setup.trackFd >= 0) ::close(h->setup.trackFd);
    h->setup.trackFd = fd;
    h->setup.trackOffset = offset;
    h->setup.trackLength = length;
}

void geode_loop_job_set_scene(geode_loop_job* h, AAssetManager* assets, const char* milk_texture_dir,
                              const char* scene_id, const char* milk_path, int entitled) {
    if (!h) return;
    try {
        h->setup.assets = assets;
        h->setup.milkTextureDir = milk_texture_dir ? milk_texture_dir : "";
        h->setup.sceneId = scene_id ? scene_id : "";
        h->setup.hasMilkPath = milk_path != nullptr;
        h->setup.milkPath = milk_path ? milk_path : "";
        h->setup.entitled = entitled != 0;
    } catch (...) {
    }
}

void geode_loop_job_set_params(geode_loop_job* h, const float* values, int count) {
    if (!h) return;
    try {
        assign(h->setup.params, values, count);
    } catch (...) {
    }
}

void geode_loop_job_set_lfo(geode_loop_job* h, int slot, const float* config, int count) {
    if (!h || slot < 0 || slot >= GEODE_LFO_SLOTS) return;
    try {
        assign(h->setup.lfos[slot], config, count);
    } catch (...) {
    }
}

void geode_loop_job_set_adsr(geode_loop_job* h, int slot, const float* config, int count) {
    if (!h || slot < 0 || slot >= GEODE_ADSR_SLOTS) return;
    try {
        assign(h->setup.adsrs[slot], config, count);
    } catch (...) {
    }
}

void geode_loop_job_set_sound_tuning(geode_loop_job* h, const float* tuning, int count) {
    if (!h) return;
    try {
        assign(h->setup.soundTuning, tuning, count);
    } catch (...) {
    }
}

void geode_loop_job_set_user_reactions(geode_loop_job* h, const float* routes, int count, int has_user) {
    if (!h) return;
    try {
        assign(h->setup.reactions, routes, count);
        h->setup.hasReactions = has_user != 0;
    } catch (...) {
    }
}

void geode_loop_job_set_motion(geode_loop_job* h, int reduced_motion, const char* transition_id,
                               int64_t transition_ms) {
    if (!h) return;
    try {
        h->setup.reducedMotion = reduced_motion != 0;
        h->setup.transitionId = transition_id ? transition_id : "";
        h->setup.transitionMs = transition_ms;
    } catch (...) {
    }
}

void geode_loop_job_set_underlay(geode_loop_job* h, const uint32_t* pixels, int count, int width, int height, int blend,
                                 float amount) {
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

void geode_loop_job_set_mark(geode_loop_job* h, const uint8_t* rgba, int tile_w, int tile_h, int placement,
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

void geode_loop_job_run(geode_loop_job* h) {
    if (!h) return;
    lr::Job* job = nullptr;
    try {
        std::lock_guard<std::mutex> lock(h->lock);
        if (h->job) return;
        h->host = lr::androidHost(h->setup);
        h->job = std::make_unique<lr::Job>(h->request, *h->host);
        if (h->cancelRequested) h->job->cancel();
        job = h->job.get();
    } catch (...) {
        h->broken.store(true, std::memory_order_release);
        return;
    }
    try {
        job->run();
    } catch (...) {
        // Out of memory mid-render: the stop closed its scene, encoder, surface and file as it unwound; leave no
        // scratch file behind.
        h->broken.store(true, std::memory_order_release);
        try {
            for (const std::string& file : job->files()) h->host->removeFile(file);
        } catch (...) {
        }
    }
}

void geode_loop_job_cancel(geode_loop_job* h) {
    if (!h) return;
    std::lock_guard<std::mutex> lock(h->lock);
    h->cancelRequested = true;
    if (h->job) h->job->cancel();
}

float geode_loop_job_progress(geode_loop_job* h) {
    const lr::Job* job = jobOf(h);
    return job ? job->progress() : 0.0f;
}

int geode_loop_job_state(geode_loop_job* h) {
    if (!h) return GEODE_LOOP_JOB_FAILED;
    if (h->broken.load(std::memory_order_acquire)) return GEODE_LOOP_JOB_FAILED;
    const lr::Job* job = jobOf(h);
    return job ? static_cast<int>(job->state()) : GEODE_LOOP_JOB_IDLE;
}

int geode_loop_job_failure(geode_loop_job* h) {
    if (!h || h->broken.load(std::memory_order_acquire)) return GEODE_LOOP_FAILURE_STOPPED;
    const lr::Job* job = jobOf(h);
    return job ? static_cast<int>(job->failure()) : GEODE_LOOP_FAILURE_NONE;
}

const char* geode_loop_job_detail(geode_loop_job* h) {
    if (!h || h->broken.load(std::memory_order_acquire)) return "";
    const lr::Job* job = jobOf(h);
    return job ? job->detail().c_str() : "";
}

int geode_loop_job_fps(geode_loop_job* h) {
    const lr::Job* job = jobOf(h);
    return job ? job->fps() : 0;
}

int64_t geode_loop_job_crossfade_ms(geode_loop_job* h) {
    const lr::Job* job = jobOf(h);
    return job ? job->crossfadeMs() : 0;
}

int geode_loop_job_file_count(geode_loop_job* h) {
    const lr::Job* job = jobOf(h);
    if (!job || job->state() != lr::State::Rendered) return 0;
    return static_cast<int>(job->files().size());
}

const char* geode_loop_job_file(geode_loop_job* h, int index) {
    if (index < 0 || index >= geode_loop_job_file_count(h)) return nullptr;
    return jobOf(h)->files()[static_cast<size_t>(index)].c_str();
}

}  // extern "C"
