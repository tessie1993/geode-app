#include "stage/LiveClient.hpp"

#include <chrono>
#include <iterator>
#include <utility>

namespace geode::stage {

LiveClient::LiveClient(std::unique_ptr<LiveViz> viz) : viz_(std::move(viz)) {}

void LiveClient::surfaceCreated() {
    if (viz_) viz_->surfaceCreated();
}

void LiveClient::surfaceChanged(int width, int height) {
    if (viz_) viz_->surfaceChanged(width, height);
}

void LiveClient::frameTime(int64_t vsyncNs) {
    if (viz_) viz_->frameTime(vsyncNs);
}

void LiveClient::framePresent(int64_t presentNs) {
    if (viz_) viz_->framePresent(presentNs);
}

uint32_t LiveClient::frame(float pacedFps, float capFps) {
    if (!viz_) return 0;
    // ADPF is the stage thread's: the whole frame, draw and swap, against the frame clock's budget (StagePort::frameWork).
    runJobs(true);
    uint32_t events = 0;
    {
        // The lock is held only to latch the ring and to hand a newer one in; the draw runs outside it.
        const void* ring;
        {
            std::lock_guard<std::mutex> sound(soundLock_);
            inFrame_ = true;
            ring = ring_;
        }
        struct Done {
            LiveClient& c;
            ~Done() {
                std::lock_guard<std::mutex> sound(c.soundLock_);
                c.inFrame_ = false;
                if (c.pendingSet_) c.ring_ = c.pending_;
                c.pendingSet_ = false;
            }
        } done{*this};
        events = viz_->frame(ring, pacedFps, capFps);
    }
    if (events == 0) return events;
    if ((events & kFrameLiquidChanged) != 0) liquid_.store(viz_->liquidMode());
    if ((events & kFrameErrorChanged) != 0) {
        std::string text = viz_->error();
        std::lock_guard<std::mutex> state(stateLock_);
        // The bit also marks a renderer's first frame: only a text that differs is news.
        if (text == error_) {
            events &= ~kFrameErrorChanged;
        } else {
            error_ = std::move(text);
        }
    }
    if ((events & kFrameDirectorMoment) != 0) {
        const int moments = viz_->takeDirectorMoments();
        if (moments > 0) {
            moments_.fetch_add(moments);
        } else {
            events &= ~kFrameDirectorMoment;
        }
    }
    if ((events & kFrameStillReady) != 0) {
        Still still;
        if (viz_->readStill(still)) {
            std::lock_guard<std::mutex> state(stateLock_);
            still_ = std::move(still);
        } else {
            // The picture was lost on the way (no memory for it): the next frame's is asked for instead.
            viz_->requestStill();
            events &= ~kFrameStillReady;
        }
    }
    return events;
}

void LiveClient::restartClock() {
    if (viz_) viz_->restartClock();
}

// The stage asks for this as it leaves drawing (Lifecycle::noteDrawing): no frame comes until it draws again.
void LiveClient::trimSpares() {
    if (!viz_) return;
    viz_->trimSpares();
    viz_->letSoundGo();
}

// Every GL name is gone and the next context hands the same numbers to other objects: a tee and its mark left open
// would draw with, and at their stop delete, names that are then the renderer's. The recording ends as failed and
// the tee closes here, with no context current, so nothing of the new context is touched.
void LiveClient::contextLost() {
    if (!viz_ || !record_) return;
    viz_->recordFailed(*record_);
    closeRecord();
}

void LiveClient::service(bool ready) { runJobs(ready); }

void LiveClient::release(bool glCurrent) {
    std::vector<Job> left;
    {
        std::lock_guard<std::mutex> jobs(jobsLock_);
        released_ = true;
        left.swap(jobs_);
    }
    {
        // A picture nobody asked for is not kept for a stage that has no renderer left.
        std::lock_guard<std::mutex> state(stateLock_);
        still_.reset();
    }
    if (!viz_) return;
    // No frame will come: a tee still waiting to open fails, and every tee asked for is closed, so no recording
    // waits on a stage that is gone.
    for (Job& job : left) {
        if (job.kind != JobKind::RecordStart) continue;
        closeRecord();
        viz_->recordFailed(job.ask);
        record_ = std::move(job.ask);
    }
    closeRecord();
    if (glCurrent) viz_->releaseScenes();
    std::unique_lock<std::shared_mutex> exclusive(vizLock_);  // waits for calls in flight on other threads
    viz_.reset();
}

void LiveClient::setSound(const void* ring) {
    std::lock_guard<std::mutex> sound(soundLock_);
    if (inFrame_) {
        pending_ = ring;  // the frame in flight keeps the ring it latched; this one lands as that frame ends
        pendingSet_ = true;
    } else {
        ring_ = ring;
        pendingSet_ = false;
    }
}

bool LiveClient::soundReleased(const void* ring) {
    std::lock_guard<std::mutex> sound(soundLock_);
    return ring_ != ring && !(pendingSet_ && pending_ == ring);
}

bool LiveClient::queue(Job job) {
    std::lock_guard<std::mutex> jobs(jobsLock_);
    if (released_) return false;
    if (job.kind == JobKind::RecordStart) recordsOpen_.fetch_add(1);
    jobs_.push_back(std::move(job));
    return true;
}

bool LiveClient::warm(std::string id) { return queue(Job{JobKind::Warm, std::move(id), {}}); }

bool LiveClient::recordStart(RecordAsk ask) { return queue(Job{JobKind::RecordStart, {}, std::move(ask)}); }

bool LiveClient::recordStop() { return queue(Job{JobKind::RecordStop, {}, {}}); }

void LiveClient::closeRecord() {
    if (!record_) return;
    viz_->recordStop(*record_);
    record_.reset();
    recordsOpen_.fetch_sub(1);
}

void LiveClient::runJobs(bool ready) {
    std::vector<Job> due;
    {
        std::lock_guard<std::mutex> jobs(jobsLock_);
        if (jobs_.empty()) return;
        due.swap(jobs_);
    }
    std::vector<Job> later;
    for (Job& job : due) {
        switch (job.kind) {
            case JobKind::Warm:
                // A warm-up builds GL objects: it waits for a context, as it waited for the surface before.
                if (ready) {
                    viz_->warmTransition(job.id);
                } else {
                    later.push_back(std::move(job));
                }
                break;
            case JobKind::RecordStart:
                closeRecord();
                // With nothing to copy from, the recording ends as failed rather than waiting for a surface.
                if (!ready || !viz_->recordStart(job.ask)) viz_->recordFailed(job.ask);
                record_ = std::move(job.ask);
                break;
            case JobKind::RecordStop:
                closeRecord();
                break;
        }
    }
    if (later.empty()) return;
    std::lock_guard<std::mutex> jobs(jobsLock_);
    jobs_.insert(jobs_.begin(), std::make_move_iterator(later.begin()), std::make_move_iterator(later.end()));
}

std::string LiveClient::error() const {
    std::lock_guard<std::mutex> state(stateLock_);
    return error_;
}

bool LiveClient::takeStill(Still& out) {
    std::lock_guard<std::mutex> state(stateLock_);
    if (!still_) return false;
    out = std::move(*still_);
    still_.reset();
    return true;
}

bool LiveClient::lockViz() {
    vizLock_.lock_shared();
    if (viz_) return true;
    vizLock_.unlock_shared();
    return false;
}

void LiveClient::unlockViz() { vizLock_.unlock_shared(); }

}  // namespace geode::stage
