#pragma once
#include <cstdint>
#include <mutex>

#include "output/RenderEta.hpp"

namespace geode::output {

// The render job: the one render the process runs at a time, its progress, its cancel request and how it
// ended (#433; ported from the Kotlin ExportRun and ExportController's phase handling). Kotlin keeps the
// coroutine, the foreground service, MediaStore and SAF, and a StateFlow that mirrors snapshot() after each
// transition; every decision about the job is here.
//
//   Idle ──begin──► Preparing ──publish──► Rendering ──finish──► Ended ──begin──► Preparing …
//                       │                      │
//                       └──requestCancel──► Cancelling ──finish──► Ended
//
// - begin is refused while a job is Preparing, Rendering or Cancelling: one render at a time, whichever
//   screen, controller or service asks (the encoder, the GPU and the foreground notification are one each).
//   From Idle or Ended it starts a fresh job: a new generation, the cancel flag down, no progress, the ETA
//   from scratch.
// - Preparing is the lead-in before any progress (the bar is indeterminate); the first publish makes it
//   Rendering. Progress is clamped to 0..1; NaN is ignored. Publishing while Cancelling still moves the bar
//   (the frame loop has not seen the flag yet) but no time left is shown for a render that is stopping.
// - requestCancel only acts on a live job (before begin or after finish it is a no-op and the next job's
//   flag stays down). The flag is what the encoders' frame loops poll.
// - finish(reported) ends a live job and resolves the outcome: a failure after a cancel request is the
//   cancel tearing the encoder down, so it reads as Cancelled; Saved stays Saved even when a cancel came
//   too late (the file is written). finish on a job that is not live changes nothing and answers None.
// - reset puts the process job back to Idle, generation 0. For tests; the app never needs it.
//
// Thread-safe: the render thread publishes and finishes, the main thread and the service cancel, the UI
// snapshots. One mutex, no allocation. Host-tested by core/tests/output_test.
enum class RenderKind : int32_t {
    Render = 0,   // the one-tap Render (one visual over one song)
    Batch = 1,    // a batch of tracks, one after another
    Project = 2,  // a Studio project or a stopped take
    Loop = 3,     // a loop render and its long-form extend
};

enum class RenderPhase : int32_t { Idle = 0, Preparing = 1, Rendering = 2, Cancelling = 3, Ended = 4 };

enum class RenderOutcome : int32_t {
    None = -1,  // no job has ended (Idle, or a job still live)
    Saved = 0,
    Failed = 1,
    Cancelled = 2,
    Locked = 3,           // a style the render draws is Pro and the entitlement does not allow it
    NothingToRender = 4,  // the plan had nothing to draw
};

// What the screen that started the job shows once it ends.
enum class RenderReport : int32_t {
    Clear = 0,     // nothing: the bar goes away (a cancel, or no job)
    Done = 1,      // the finished video, with Open and Share
    Failed = 2,    // the reason
    OfferPro = 3,  // the paywall for the locked style, the bar cleared
};

RenderReport reportFor(RenderOutcome outcome);

// Whether what the job wrote stays: only a saved video does. Every other outcome deletes a target the job
// created (#366): a locked style, nothing to render, a failure, a cancel.
bool keepsOutput(RenderOutcome outcome);

struct RenderJobSnapshot {
    RenderPhase phase = RenderPhase::Idle;
    RenderKind kind = RenderKind::Render;
    uint32_t generation = 0;  // counts begins, so a mirror can tell one job from the next
    bool hasProgress = false;
    float progress = 0.0f;
    int64_t secondsLeft = -1;  // -1 while there is no estimate (output/RenderEta), or while cancelling
    bool cancelRequested = false;
    RenderOutcome outcome = RenderOutcome::None;  // set once Ended

    bool live() const {
        return phase == RenderPhase::Preparing || phase == RenderPhase::Rendering || phase == RenderPhase::Cancelling;
    }
};

class RenderJob {
public:
    // The process's one job, behind geode_render_job_*.
    static RenderJob& process();

    // False (and nothing changes) while a job is live.
    bool begin(RenderKind kind);
    // False when no job is live or progress is NaN. steady and atMs feed the time-left estimate.
    bool publish(float progress, int64_t atMs, bool steady);
    // True when it raised the flag on a live job; false when there is no live job or the flag was up.
    bool requestCancel();
    // The resolved outcome, or None when no job was live.
    RenderOutcome finish(RenderOutcome reported);
    void reset();

    RenderJobSnapshot snapshot() const;
    bool cancelRequested() const;

private:
    mutable std::mutex mutex_;
    RenderJobSnapshot state_;
    RenderEtaState eta_;
};

}  // namespace geode::output
