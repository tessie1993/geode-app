package dev.geode.playback

import android.os.SystemClock
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import dev.geode.analysis.PlaybackMath
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume

class SleepTimer internal constructor(
    private val player: Player,
    private val scope: CoroutineScope,
) {
    private val _remainingMs = MutableStateFlow<Long?>(null)

    val remainingMs: StateFlow<Long?> = _remainingMs

    @Volatile
    var onFadeVolume: ((Float) -> Unit)? = null

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
                        if (!finishTrack) applyFade(PlaybackMath.sleepFadeVolume(remaining))
                        delay(if (remaining <= PlaybackMath.SLEEP_FADE_MS) 100 else 500)
                    }
                    if (finishTrack && playbackOngoing()) {
                        _remainingMs.value = 0L
                        awaitCurrentTrackDone()
                    }
                    player.pause()
                } finally {
                    applyFade(1f)
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
        applyFade(1f)
    }

    private fun applyFade(volume: Float) {
        val hook = onFadeVolume
        if (hook != null) hook(volume) else player.volume = volume
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
}
