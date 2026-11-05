package dev.geode.export

import dev.geode.engine.bridge.GeodeNative

/** One track a render covers: its audio URI (as a string, so this file stays pure) and its title. */
data class BatchTrack(
    val uri: String,
    val title: String,
)

/** A playlist the Render tab can batch: saved or smart, already resolved to its tracks. */
data class RenderPlaylist(
    val name: String,
    val tracks: List<BatchTrack>,
)

/**
 * What Studio › Render's Render button covers. Only [CURRENT_TRACK] is a single render; every
 * other scope is a batch, rendered one track after another, and batches are Pro. Ordinals are native codes.
 */
enum class RenderScope {
    CURRENT_TRACK,

    /** What is queued in the Player right now (the glossary's queue, not a saved playlist). */
    PLAY_QUEUE,

    /** One saved or smart playlist, picked on the Render tab. */
    PLAYLIST,

    /** Tracks ticked in the Render tab's track checklist. */
    CHOSEN_TRACKS,
    ;

    val isBatch: Boolean get() = this != CURRENT_TRACK

    companion object {
        /**
         * Android 10 (API 29), the first release where a batch can work. A batch has no
         * destination picker: every track goes into Movies/MuzViz through MediaStore, with
         * RELATIVE_PATH and IS_PENDING. Before scoped storage, inserting into the shared Videos
         * collection needs WRITE_EXTERNAL_STORAGE, which Geode does not declare (it asks for
         * nothing it can do without), so on Android 8 and 9 every track of a batch failed at
         * the insert. A single render there can still save to a folder the user picks.
         */
        const val BATCH_MIN_SDK: Int = 29

        /** Whether batches can run on a device at [sdkInt]; see [BATCH_MIN_SDK]. */
        fun batchSupported(sdkInt: Int): Boolean = sdkInt >= BATCH_MIN_SDK

        /** The tracks [scope] renders, natively picked from every list (numbered by scope): its own, in order, each URI once. */
        fun tracksFor(
            scope: RenderScope,
            current: BatchTrack?,
            queue: List<BatchTrack>,
            playlist: List<BatchTrack>,
            chosen: List<BatchTrack>,
        ): List<BatchTrack> {
            val lists = listOf(listOfNotNull(current), queue, playlist, chosen)
            val candidates = lists.flatten()
            val sources = lists.flatMapIndexed { source, list -> List(list.size) { source } }.toIntArray()
            return GeodeNative.renderBatchTracks(scope.ordinal, candidates.map { it.uri }.toTypedArray(), sources).map { candidates[it] }
        }
    }
}

/** How one track of a batch ended; [code] is its native outcome ([ExportRun.Outcome] ordinal). */
sealed class BatchOutcome(
    val code: Int,
) {
    /** [fileName] is null when the file went to a folder the user picked. */
    data class Saved(
        val uri: String,
        val fileName: String?,
    ) : BatchOutcome(ExportRun.Outcome.SAVED.ordinal)

    data class Failed(
        val message: String,
    ) : BatchOutcome(ExportRun.Outcome.FAILED.ordinal)

    /** The batch was cancelled before (or while) this track rendered. */
    data object Cancelled : BatchOutcome(ExportRun.Outcome.CANCELLED.ordinal)
}

/**
 * A batch's queue: the tracks and how each finished so far, the app data `core/output/RenderBatch` indexes. Native
 * decides what a failure (skip, go on) or a cancel (it and every track after) does, the progress and the bar ([report]).
 */
data class RenderBatch(
    val tracks: List<BatchTrack>,
    val outcomes: List<BatchOutcome> = emptyList(),
) {
    private val summary = IntArray(GeodeNative.RENDER_BATCH_INTS)
    private val failedAt = GeodeNative.renderBatchSummarize(outcomes.map { it.code }.toIntArray(), tracks.size, summary)

    /** The track rendering now, or null once every track has an outcome. */
    val current: BatchTrack? get() = tracks.getOrNull(summary[GeodeNative.RENDER_BATCH_CURRENT])

    val savedCount: Int get() = summary[GeodeNative.RENDER_BATCH_SAVED]

    val failures: List<Pair<BatchTrack, String>> get() = failedAt.map { tracks[it] to (outcomes[it] as BatchOutcome.Failed).message }

    val lastSaved: BatchOutcome.Saved? get() = outcomes.getOrNull(summary[GeodeNative.RENDER_BATCH_LAST_SAVED]) as? BatchOutcome.Saved

    val report: Int get() = summary[GeodeNative.RENDER_BATCH_REPORT]

    /** The track to render next, or null when none is left or [cancelRequested]. */
    fun next(cancelRequested: Boolean): BatchTrack? =
        tracks.getOrNull(GeodeNative.renderBatchNext(tracks.size, outcomes.size, cancelRequested))

    /** The current track's outcome; native says how many tracks it ends (a cancel ends the rest). */
    fun record(outcome: BatchOutcome): RenderBatch {
        val ended = GeodeNative.renderBatchRecord(tracks.size, outcomes.size, outcome.code) - outcomes.size
        return if (ended <= 0) this else copy(outcomes = outcomes + List(ended) { if (it == 0) outcome else BatchOutcome.Cancelled })
    }

    /** The whole batch's progress, with [withinCurrent] the share of the current track done. */
    fun progress(withinCurrent: Float): Float = GeodeNative.renderBatchProgress(tracks.size, outcomes.size, withinCurrent)
}
