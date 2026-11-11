package dev.geode.export

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
 * other scope is a batch, rendered one track after another, and batches are Pro.
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
         * The tracks [scope] renders, in order, each once: a queue or a hand-made list can hold the
         * same track twice, and rendering it twice would only make two identical files.
         */
        fun tracksFor(
            scope: RenderScope,
            current: BatchTrack?,
            queue: List<BatchTrack>,
            playlist: List<BatchTrack>,
            chosen: List<BatchTrack>,
        ): List<BatchTrack> =
            when (scope) {
                CURRENT_TRACK -> listOfNotNull(current)
                PLAY_QUEUE -> queue
                PLAYLIST -> playlist
                CHOSEN_TRACKS -> chosen
            }.distinctBy { it.uri }
    }
}

/** How one track of a batch ended. */
sealed interface BatchOutcome {
    /** [fileName] is null when the file went to a folder the user picked. */
    data class Saved(
        val uri: String,
        val fileName: String?,
    ) : BatchOutcome

    data class Failed(
        val message: String,
    ) : BatchOutcome

    /** The batch was cancelled before (or while) this track rendered. */
    data object Cancelled : BatchOutcome
}

/**
 * A batch render's queue: the tracks in order and how each finished so far. A failure is recorded
 * and the next track starts (the user chose skip-and-continue over stopping), so a batch ends with
 * every track accounted for, which is what the end-of-batch summary reads.
 */
data class RenderBatch(
    val tracks: List<BatchTrack>,
    val outcomes: List<BatchOutcome> = emptyList(),
) {
    val finished: Boolean get() = outcomes.size >= tracks.size

    /** The track rendering now, or null once every track has an outcome. */
    val current: BatchTrack? get() = tracks.getOrNull(outcomes.size)

    val savedCount: Int get() = outcomes.count { it is BatchOutcome.Saved }

    val failures: List<Pair<BatchTrack, String>>
        get() =
            outcomes.zip(tracks).mapNotNull { (outcome, track) ->
                (outcome as? BatchOutcome.Failed)?.let { track to it.message }
            }

    val lastSaved: BatchOutcome.Saved? get() = outcomes.lastOrNull { it is BatchOutcome.Saved } as BatchOutcome.Saved?

    /** The current track's outcome; a batch that has finished ignores late ones. */
    fun record(outcome: BatchOutcome): RenderBatch = if (finished) this else copy(outcomes = outcomes + outcome)

    fun cancelRest(): RenderBatch = copy(outcomes = outcomes + List(tracks.size - outcomes.size) { BatchOutcome.Cancelled })

    /** The whole batch's progress, with [withinCurrent] the share of the current track done. */
    fun progress(withinCurrent: Float): Float {
        if (tracks.isEmpty() || finished) return 1f
        return (outcomes.size + withinCurrent.coerceIn(0f, 1f)) / tracks.size
    }
}
