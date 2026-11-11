package dev.geode.playback

import androidx.media3.common.PlaybackException
import androidx.media3.common.Player

/**
 * What the player does when a track will not play: skip to the next one, or stop when nothing
 * follows or several in a row have failed ([PlaybackErrors.decide]).
 *
 * WHY here and not in the UI: the session can run with no UI at all (Android Auto, a headset
 * button, System UI resuming playback). The recovery used to be a listener the UI added, so in
 * those cases a deleted or unreadable file stopped playback for good with nothing skipped. The UI
 * now only reads [last] to say what happened.
 */
class PlaybackErrorRecovery(
    private val player: Player,
) : Player.Listener {
    data class Failure(
        val errorCode: Int,
        val title: String?,
        val action: PlaybackErrors.Action,
    )

    /** The most recent failure and what was done about it. Main thread, like every player callback. */
    var last: Failure? = null
        private set

    private var consecutiveFailures = 0

    override fun onPlayerError(error: PlaybackException) {
        consecutiveFailures++
        // Read before the skip: afterwards the current item is the next track.
        val title =
            player.currentMediaItem
                ?.mediaMetadata
                ?.title
                ?.toString()
        val action = PlaybackErrors.decide(consecutiveFailures, hasNext = player.hasNextMediaItem())
        last = Failure(error.errorCode, title, action)
        when (action) {
            PlaybackErrors.Action.SkipToNext -> {
                player.seekToNextMediaItem()
                player.prepare()
            }

            PlaybackErrors.Action.StopEndOfQueue,
            PlaybackErrors.Action.StopSourceUnavailable,
            -> player.pause()
        }
    }

    override fun onIsPlayingChanged(isPlaying: Boolean) {
        if (isPlaying) consecutiveFailures = 0
    }
}
