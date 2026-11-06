package dev.geode.playback

import android.os.SystemClock
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume

/**
 * Settings › Playback › Sleep timer. The countdown and the pause are here; the fade over its last
 * seconds is native (`geode_dsp_sleep_fade`, `core/audio/dsp/GainStage`): [fade] is told the time left
 * and answers how long until it next needs to hear it. A timer that finishes the track does not fade.
 */
class SleepTimer internal constructor(
    private val player: Player,
    private val scope: CoroutineScope,
    /** The native sleep fade: the time left in ms (< 0 cancels), returning the ms until the next report. */
    private val fade: (remainingMs: Long) -> Long,
) {
    private val _remainingMs = MutableStateFlow<Long?>(null)

    val remainingMs: StateFlow<Long?> = _remainingMs

    private var job: Job? = null

    val isRunning: Boolean
        get() = job?.isActive == true

    fun start(
        minutes: Int,
        finishTrack: Boolean,
    ) {
        cancel()
        if (minutes <= 0) return
        lateinit var started: Job
        started =
            scope.launch {
                try {
                    val endMs = SystemClock.elapsedRealtime() + minutes * 60_000L
                    while (true) {
                        val remaining = endMs - SystemClock.elapsedRealtime()
                        if (remaining <= 0L) break
                        _remainingMs.value = remaining
                        val wanted = if (finishTrack) TICK_MS else fade(remaining)
                        delay(wanted.coerceIn(1L, TICK_MS))
                    }
                    _remainingMs.value = 0L
                    if (finishTrack && playbackOngoing()) {
                        awaitCurrentTrackDone()
                    } else if (!finishTrack) {
                        // The fade ended on the samples, which run a sink buffer ahead of the speaker.
                        delay(PlaybackSession.SINK_LAG_MS)
                    }
                    player.pause()
                } finally {
                    fade(-1L)
                    if (job === started) {
                        job = null
                        _remainingMs.value = null
                    }
                }
            }
        job = started
    }

    fun cancel() {
        val running = job
        job = null
        _remainingMs.value = null
        running?.cancel()
        fade(-1L)
    }

    private fun playbackOngoing(): Boolean =
        player.playWhenReady &&
            player.playbackState != Player.STATE_IDLE &&
            player.playbackState != Player.STATE_ENDED

    private suspend fun awaitCurrentTrackDone() =
        suspendCancellableCoroutine { cont ->
            val listener =
                object : Player.Listener {
                    override fun onMediaItemTransition(
                        mediaItem: MediaItem?,
                        reason: Int,
                    ) = done()

                    override fun onPositionDiscontinuity(
                        oldPosition: Player.PositionInfo,
                        newPosition: Player.PositionInfo,
                        reason: Int,
                    ) {
                        if (reason == Player.DISCONTINUITY_REASON_AUTO_TRANSITION) done()
                    }

                    override fun onPlaybackStateChanged(playbackState: Int) {
                        if (playbackState == Player.STATE_ENDED || playbackState == Player.STATE_IDLE) done()
                    }

                    override fun onPlayWhenReadyChanged(
                        playWhenReady: Boolean,
                        reason: Int,
                    ) {
                        if (!playWhenReady) done()
                    }

                    private fun done() {
                        player.removeListener(this)
                        if (cont.isActive) cont.resume(Unit)
                    }
                }
            player.addListener(listener)
            cont.invokeOnCancellation { player.removeListener(listener) }
        }

    private companion object {
        /** How often the countdown the UI shows moves. */
        const val TICK_MS = 500L
    }
}
