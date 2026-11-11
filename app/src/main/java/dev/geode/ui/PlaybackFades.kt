package dev.geode.ui

import androidx.annotation.OptIn
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.common.util.Util
import dev.geode.playback.PlaybackSession
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Settings › Playback › Fade: pause, resume and skip fade out and back in. When to fade, and what to do
 * once a fade-out is heard, is decided here; the ramp itself is native (`geode_dsp_ramp_to`), linear in
 * dB on the samples, so it has no steps (#483). The samples run up to a sink buffer ahead of the speaker,
 * so an action after a fade-out waits the fade plus [PlaybackSession.SINK_LAG_MS], and a resume after
 * one of these pauses fades back in from the silence it left in that buffer.
 */
internal class PlaybackFades(
    private val scope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        val player: Player
        val fadeMs: Int

        /** The native UI fade, to full level ([audible]) or to silence, over [ms]. */
        fun rampTo(
            audible: Boolean,
            ms: Int,
        )
    }

    /** The UI fade was last sent to silence and not yet brought back. */
    private var silenced = false

    private var fadeJob: Job? = null

    /** Fades to silence, then runs [then] once the faded audio has reached the speaker. */
    private fun fadeOutThen(then: () -> Unit) {
        val durationMs = host.fadeMs
        fadeJob?.cancel()
        silenced = true
        host.rampTo(false, durationMs)
        lateinit var self: Job
        self =
            scope.launch {
                delay(durationMs + PlaybackSession.SINK_LAG_MS)
                then()
                // `then()` (skipFaded's fade-in) may have started a new fadeJob; only clear our own.
                if (fadeJob === self) fadeJob = null
            }
        fadeJob = self
    }

    private fun fadeIn() {
        fadeJob?.cancel()
        fadeJob = null
        silenced = false
        host.rampTo(true, host.fadeMs)
    }

    @OptIn(UnstableApi::class)
    fun togglePlayPauseFaded() {
        val player = host.player
        if (player.isPlaying && host.fadeMs > 0) {
            fadeOutThen { player.pause() }
        } else if (player.isPlaying) {
            player.pause()
        } else {
            // Only a pause of ours left silence to fade in from. One from elsewhere (the notification, a
            // headset) left full-level audio in the sink buffer, which plays first on resume, so dipping
            // to silence after it would be a dropout, not a fade.
            if (silenced) fadeIn()
            // play() alone is a no-op once the track has ENDED (repeat off); this rewinds first.
            Util.handlePlayButtonAction(player)
        }
    }

    fun skipFaded(action: () -> Unit) {
        if (host.fadeMs <= 0 || !host.player.isPlaying) {
            action()
            return
        }
        fadeOutThen {
            action()
            fadeIn()
        }
    }

    fun ensureAudibleAfterExternalPlay() {
        if (silenced && fadeJob?.isActive != true) fadeIn()
    }
}
