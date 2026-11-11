package dev.geode.playback

import dev.geode.data.SessionStore

/**
 * Where playback resumption (#877) gets its queue: the one rule [PlaybackService.onPlaybackResumption]
 * and the app's own start path both ask, so they cannot drift.
 *
 * The rule is: resume the saved queue, at its saved track and position, or resume nothing. There is no
 * fallback to the last history entry. That fallback rebuilt a one-track queue at 0:00 and so showed the
 * user a different queue and a restarted track whenever the session file was missing or quarantined;
 * resuming nothing is honest, and the next play rewrites the file ([PlayRecorder]).
 */
sealed interface ResumeSource {
    /** The queue to restore, with the index and position already inside the queue. */
    data class Queue(
        val saved: SessionStore.Saved,
    ) : ResumeSource

    /** No usable saved session: leave the player empty. */
    data object Nothing : ResumeSource

    companion object {
        /** Decides from what [SessionStore.load] returned (null for a missing or quarantined file). */
        fun of(saved: SessionStore.Saved?): ResumeSource {
            if (saved == null || saved.tracks.isEmpty()) return Nothing
            return Queue(
                saved.copy(
                    index = saved.index.coerceIn(0, saved.tracks.size - 1),
                    positionMs = saved.positionMs.coerceAtLeast(0L),
                ),
            )
        }
    }
}
