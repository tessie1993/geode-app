// The C ABI over a loop extend's copy (geode_loop_mux_api.h): what the shell hands over is gathered into the job's
// request (output/LoopMux) and the phone host's setup (output/android/LoopMuxHost), and run() wires the two. The
// copy, the interleave and the writer are the job's, host-tested in core/tests/loop_mux_test.cpp; nothing is
// decided here, and no exception leaves it.
#include "api/geode_loop_mux_api.h"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "api/AacJobTrack.hpp"
#include "output/LoopExtend.hpp"
#include "output/LoopMux.hpp"
#include "output/android/LoopMuxHost.hpp"

namespace lm = geode::output::loopmux;

struct geode_loop_mux {
    ~geode_loop_mux() {
        for (const int fd : setup.clipFds) {
            if (fd >= 0) ::close(fd);
        }
    }

    lm::Request request;
    lm::Setup setup;
    // The first clip's shape; every later clip must match it.
    int rateHz = 0;
    int channels = 0;
    std::unique_ptr<lm::Host> host;
    // Guards `job` between the run that makes it and the polls that read it; the job's own state is atomic.
    std::mutex lock;
    std::unique_ptr<lm::Job> job;
    bool cancelRequested = false;
    // run() could not start the job, or the job threw: reported as a failure with no text.
    std::atomic<bool> broken{false};
};

static_assert(static_cast<int>(lm::State::Idle) == GEODE_LOOP_MUX_IDLE);
static_assert(static_cast<int>(lm::State::Running) == GEODE_LOOP_MUX_RUNNING);
static_assert(static_cast<int>(lm::State::Done) == GEODE_LOOP_MUX_DONE);
static_assert(static_cast<int>(lm::State::Cancelled) == GEODE_LOOP_MUX_CANCELLED);
static_assert(static_cast<int>(lm::State::Failed) == GEODE_LOOP_MUX_FAILED);
static_assert(static_cast<int>(lm::Failure::None) == GEODE_LOOP_MUX_FAILURE_NONE);
static_assert(static_cast<int>(lm::Failure::Unusable) == GEODE_LOOP_MUX_FAILURE_UNUSABLE);
static_assert(static_cast<int>(lm::Failure::Read) == GEODE_LOOP_MUX_FAILURE_READ);
static_assert(static_cast<int>(lm::Failure::Write) == GEODE_LOOP_MUX_FAILURE_WRITE);

namespace {

// The job once run() has made it, else null.
lm::Job* jobOf(geode_loop_mux* h) {
    if (!h) return nullptr;
    std::lock_guard<std::mutex> lock(h->lock);
    return h->job.get();
}

}  // namespace

extern "C" {

geode_loop_mux* geode_loop_mux_create(void) {
    try {
        return new geode_loop_mux();
    } catch (...) {
        return nullptr;
    }
}

void geode_loop_mux_destroy(geode_loop_mux* h) { delete h; }

void geode_loop_mux_add_loop(geode_loop_mux* h, const char* path) {
    if (!h || !path) return;
    try {
        h->setup.loopPaths.emplace_back(path);
    } catch (...) {
    }
}

int geode_loop_mux_add_clip(geode_loop_mux* h, const geode_aac_job* job, const char* scratch_path) {
    if (!h || !job || !scratch_path) return GEODE_LOOP_MUX_CLIP_UNREADABLE;
    try {
        geode::api::AacTrack track;
        if (!geode::api::readAacTrack(job, &track)) return GEODE_LOOP_MUX_CLIP_UNREADABLE;
        if (!h->request.clips.empty() && (track.rateHz != h->rateHz || track.channels != h->channels)) {
            return GEODE_LOOP_MUX_CLIP_DIFFERS;
        }
        const int fd = ::open(scratch_path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return GEODE_LOOP_MUX_CLIP_UNREADABLE;
        h->setup.clipFds.push_back(fd);
        lm::Clip clip;
        clip.samples = std::move(track.samples);
        clip.durationUs = track.durationUs;
        h->request.clips.push_back(std::move(clip));
        if (h->request.clips.size() == 1) {
            h->rateHz = track.rateHz;
            h->channels = track.channels;
            h->request.audioFormat = std::move(track.format);
        }
        return GEODE_LOOP_MUX_CLIP_ADDED;
    } catch (...) {
        return GEODE_LOOP_MUX_CLIP_UNREADABLE;
    }
}

int64_t geode_loop_mux_total_us(geode_loop_mux* h) {
    if (!h) return 0;
    int64_t total = 0;
    for (const lm::Clip& clip : h->request.clips) total += clip.durationUs;
    return total;
}

void geode_loop_mux_run(geode_loop_mux* h, int output_fd, int64_t loop_duration_us) {
    if (!h) return;
    lm::Job* job = nullptr;
    try {
        std::lock_guard<std::mutex> lock(h->lock);
        if (h->job) return;
        h->setup.outputFd = output_fd;
        h->request.loops = static_cast<int>(h->setup.loopPaths.size());
        h->request.loopDurationUs = loop_duration_us;
        h->host = lm::androidHost(h->setup);
        h->job = std::make_unique<lm::Job>(h->request, *h->host);
        if (h->cancelRequested) h->job->cancel();
        job = h->job.get();
    } catch (...) {
        h->broken.store(true, std::memory_order_release);
        return;
    }
    try {
        job->run();
    } catch (...) {
        // Out of memory mid-copy: the muxer was released as the job unwound; the shell discards the file.
        h->broken.store(true, std::memory_order_release);
    }
}

void geode_loop_mux_cancel(geode_loop_mux* h) {
    if (!h) return;
    std::lock_guard<std::mutex> lock(h->lock);
    h->cancelRequested = true;
    if (h->job) h->job->cancel();
}

float geode_loop_mux_progress(geode_loop_mux* h) {
    const lm::Job* job = jobOf(h);
    return job ? job->progress() : geode::output::loopext::kAudioProgressSpan;
}

int geode_loop_mux_state(geode_loop_mux* h) {
    if (!h || h->broken.load(std::memory_order_acquire)) return GEODE_LOOP_MUX_FAILED;
    const lm::Job* job = jobOf(h);
    return job ? static_cast<int>(job->state()) : GEODE_LOOP_MUX_IDLE;
}

int geode_loop_mux_failure(geode_loop_mux* h) {
    if (!h || h->broken.load(std::memory_order_acquire)) return GEODE_LOOP_MUX_FAILURE_WRITE;
    const lm::Job* job = jobOf(h);
    return job ? static_cast<int>(job->failure()) : GEODE_LOOP_MUX_FAILURE_NONE;
}

const char* geode_loop_mux_detail(geode_loop_mux* h) {
    if (!h || h->broken.load(std::memory_order_acquire)) return "";
    const lm::Job* job = jobOf(h);
    return job ? job->detail().c_str() : "";
}

}  // extern "C"
