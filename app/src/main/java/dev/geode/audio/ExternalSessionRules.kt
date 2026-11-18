package dev.geode.audio

import android.media.session.PlaybackState

/** What the player's transport may ask of another app's media session: what its PlaybackState actions allow. */
data class ExternalControls(
    val playPause: Boolean = false,
    val next: Boolean = false,
    val previous: Boolean = false,
    val seek: Boolean = false,
)

/** How play/pause reaches the other app: the session's own play or pause, or its play/pause key when that is all it takes. */
enum class ExternalToggle { PLAY, PAUSE, PLAY_PAUSE_KEY, NONE }

/**
 * The decisions behind [NowPlayingBridge], kept apart from the platform objects so they are tested on the JVM:
 * which session is the other app's, what its transport allows, and where its playhead is now.
 */
object ExternalSessionRules {
    /** One active session as [choose] sees it: its app, and its PlaybackState's state (null: none reported). */
    data class Candidate(
        val packageName: String,
        val state: Int?,
    )

    /** Moving, or about to: the app is playing as far as the person is concerned. */
    fun isPlaying(state: Int?): Boolean =
        when (state) {
            PlaybackState.STATE_PLAYING,
            PlaybackState.STATE_BUFFERING,
            PlaybackState.STATE_CONNECTING,
            PlaybackState.STATE_FAST_FORWARDING,
            PlaybackState.STATE_REWINDING,
            PlaybackState.STATE_SKIPPING_TO_NEXT,
            PlaybackState.STATE_SKIPPING_TO_PREVIOUS,
            PlaybackState.STATE_SKIPPING_TO_QUEUE_ITEM,
            -> true
            else -> false
        }

    /**
     * The index in [candidates] (the platform's order, most relevant first) of the session to show and drive, -1 for
     * none. Never [ownPackage]'s. A playing session wins, the one already shown first; with none playing the one
     * already shown ([currentPackage]) stays, so pausing it does not hand the transport to some other paused app.
     */
    fun choose(
        candidates: List<Candidate>,
        ownPackage: String,
        currentPackage: String?,
    ): Int {
        val others = candidates.indices.filter { candidates[it].packageName != ownPackage }
        val current = others.firstOrNull { candidates[it].packageName == currentPackage }
        if (current != null && isPlaying(candidates[current].state)) return current
        others.firstOrNull { isPlaying(candidates[it].state) }?.let { return it }
        return current ?: others.firstOrNull() ?: -1
    }

    /** What [actions] (PlaybackState.getActions) allows in [state]; seeking also needs a known [durationMs]. */
    fun controls(
        actions: Long,
        state: Int?,
        durationMs: Long,
    ): ExternalControls {
        if (state == null) return ExternalControls()
        return ExternalControls(
            playPause = toggle(actions, state) != ExternalToggle.NONE,
            next = (actions and PlaybackState.ACTION_SKIP_TO_NEXT) != 0L,
            previous = (actions and PlaybackState.ACTION_SKIP_TO_PREVIOUS) != 0L,
            seek = durationMs > 0 && (actions and PlaybackState.ACTION_SEEK_TO) != 0L,
        )
    }

    /** Pause a playing session and play a stopped one, by the action it declares; its play/pause key otherwise. */
    fun toggle(
        actions: Long,
        state: Int?,
    ): ExternalToggle {
        if (state == null) return ExternalToggle.NONE
        val direct = if (isPlaying(state)) PlaybackState.ACTION_PAUSE else PlaybackState.ACTION_PLAY
        return when {
            (actions and direct) != 0L -> if (isPlaying(state)) ExternalToggle.PAUSE else ExternalToggle.PLAY
            (actions and PlaybackState.ACTION_PLAY_PAUSE) != 0L -> ExternalToggle.PLAY_PAUSE_KEY
            else -> ExternalToggle.NONE
        }
    }

    /**
     * The playhead at [nowMs] (elapsedRealtime): the session reports a position at [updatedAtMs] and moves it at
     * [speed] while [playing]. Within 0..[durationMs] when the duration is known; 0 when the position is unknown.
     */
    fun positionAt(
        positionMs: Long,
        updatedAtMs: Long,
        speed: Float,
        playing: Boolean,
        durationMs: Long,
        nowMs: Long,
    ): Long {
        if (positionMs < 0) return 0L
        val moved =
            if (playing && updatedAtMs > 0 && nowMs > updatedAtMs) {
                positionMs + ((nowMs - updatedAtMs) * speed).toLong()
            } else {
                positionMs
            }
        val clamped = moved.coerceAtLeast(0L)
        return if (durationMs > 0) clamped.coerceAtMost(durationMs) else clamped
    }
}
