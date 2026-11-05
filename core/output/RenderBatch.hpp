#pragma once
#include <cstdint>
#include <string_view>

#include "output/RenderJob.hpp"

namespace geode::output {

// A batch render's bookkeeping (Studio › Render with any scope but the song playing; ported from the Kotlin
// export/RenderBatch and ExportController.startBatch): which tracks a scope covers and in what order, which
// track renders next, the whole batch's progress, what a track's outcome or a cancel does to the rest, and
// the end-of-batch summary with the bar it shows.
//
// Stateless on purpose. The batch is one RenderJob (RenderKind::Batch), which already owns "one render at a
// time", the cancel flag and the bar's progress; what a batch adds is a pure function of the track count and
// the outcomes so far. The tracks themselves (URIs and titles from the library) and each outcome's strings
// (the saved URI and file name, the failure message) are app data that stays Kotlin, indexed the same way, so
// a native copy of the queue would only be a second list to keep in step, and a process-wide one would also
// have to outlive the controller that started it. Kotlin holds the outcome codes (RenderOutcome: Saved,
// Failed, Cancelled) in track order and asks here for every decision.
//
// - A failed track is recorded and the next one starts (skip and continue, the owner's choice over stopping).
// - A Cancelled outcome (the user's cancel stopping the track rendering now) marks it and every track after
//   it Cancelled; files already finished stay. A batch that has an outcome for every track ignores late ones.
// - The summary's bar: the last saved video (Done) when anything saved, else the first failure (Failed),
//   else nothing (Clear), where a cancelled batch that saved nothing still shows its counts.
// Host-tested by core/tests/output_test.

// What the Render button covers; the same order as Kotlin's RenderScope. Each scope reads its own candidate
// source, numbered alike: the song playing, the play queue, the picked playlist, the ticked tracks.
enum class BatchScope : int32_t { CurrentTrack = 0, PlayQueue = 1, Playlist = 2, ChosenTracks = 3 };

// The tracks `scope` renders: indices into the candidates (keys[i], the track's URI, from source sources[i]),
// those of the scope's source in the order given, each URI once (a queue or a hand-made list can hold a track
// twice, and rendering it twice only makes two identical files). Writes min(n, cap) indices, returns n; an
// unknown scope covers nothing.
int batchTracks(BatchScope scope, const std::string_view* keys, const int32_t* sources, int count, int32_t* out,
                int cap);

// The track that renders next: `done` while one is left and no cancel was requested, else -1.
int batchNext(int total, int done, bool cancelRequested);

// The outcome count after the current track (index `done`) ended with `outcome`: one more, or every track
// when it was cancelled (the rest are Cancelled too). A finished batch stays as it is. Any outcome other than
// Saved or Cancelled is a failure. Writes the new codes into codes[done..) when codes is not null.
int batchRecord(int32_t* codes, int total, int done, RenderOutcome outcome);

// The whole batch's progress, with `within` the share of the current track done (clamped to 0..1, NaN passed
// on for RenderJob::publish to drop, as the Kotlin did): finished tracks plus that share, over all of them;
// 1 when the batch is empty or done.
float batchProgress(int total, int done, float within);

struct BatchSummary {
    int32_t total = 0;
    int32_t done = 0;
    int32_t saved = 0;
    int32_t failed = 0;
    int32_t cancelled = 0;
    int32_t current = -1;      // the track rendering now; -1 once every track has an outcome
    int32_t lastSaved = -1;    // the newest saved track, whose video the bar opens and shares
    int32_t firstFailed = -1;  // whose reason the bar shows when nothing saved
    RenderReport report = RenderReport::Clear;
    bool finished = false;
};

// The summary of `done` outcome codes for a batch of `total` tracks.
BatchSummary summarizeBatch(const int32_t* codes, int total, int done);

// The failed tracks among the first `done` codes, in track order (the bar lists each with its reason). Writes
// min(n, cap) indices, returns n.
int batchFailed(const int32_t* codes, int done, int32_t* out, int cap);

}  // namespace geode::output
