package dev.geode.playback

import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Settings › Playback › Crossfade. One track fades out and the next fades in, back to back on the
 * one player. It is not an overlap: two tracks sounding at once would need a second player mixed
 * into the tap, and the tap stays first and single (docs/AUDIO_CHAIN.md). The owner chose the
 * setting, and this is the shape that keeps the audio chain as it is.
 *
 * Half of [crossfadeMs] is spent on each side of the join. The gain goes to [onGain], and
 * [PlaybackSession] multiplies it with the pause and sleep fades, so none of them overwrites
 * another. The tap sits before the volume, so the visuals keep hearing the music at full level
 * through the fade.
 *
 * It only joins tracks that follow on by themselves. A skip, a seek or a pick from the queue starts
 * at full volume, as gapless did.
 */
internal class TrackCrossfade(
    private val player: Player,
    private val scope: CoroutineScope,
    private val onGain: (Float) -> Unit,
) : Player.Listener {
    var crossfadeMs: Int = 0
        set(value) {
            field = value.coerceAtLeast(0)
            if (field == 0) {
                stop()
            } else if (player.isPlaying) {
                start()
            }
        }

    /** The current track was reached because the previous one ended, so it fades in. */
    private var fadingIn = false

    private var job: Job? = null

    override fun onIsPlayingChanged(isPlaying: Boolean) {
        if (isPlaying && crossfadeMs > 0) start() else job?.cancel()
    }

    override fun onMediaItemTransition(
        mediaItem: MediaItem?,
        reason: Int,
    ) {
        val followedOn = reason == Player.MEDIA_ITEM_TRANSITION_REASON_AUTO || reason == Player.MEDIA_ITEM_TRANSITION_REASON_REPEAT
        fadingIn = crossfadeMs > 0 && followedOn
        if (!fadingIn) onGain(1f)
    }

    private fun start() {
        if (job?.isActive == true) return
        job =
            scope.launch {
                while (isActive) {
                    onGain(gainNow())
                    delay(STEP_MS)
                }
            }
    }

    private fun stop() {
        job?.cancel()
        job = null
        fadingIn = false
        onGain(1f)
    }

    private fun gainNow(): Float {
        val half = crossfadeMs / 2L
        if (half <= 0L) return 1f
        val position = player.currentPosition
        if (fadingIn && position >= half) fadingIn = false
        val fadeIn = if (fadingIn) position.toFloat() / half else 1f
        val followsOn = player.hasNextMediaItem() || player.repeatMode == Player.REPEAT_MODE_ONE
        val duration = player.duration
        val remaining = if (duration > 0L) duration - position else Long.MAX_VALUE
        val fadeOut = if (followsOn && remaining < half) remaining.toFloat() / half else 1f
        return smooth(minOf(fadeIn, fadeOut).coerceIn(0f, 1f))
    }

    /** Smoothstep, so neither end of the ramp clicks. */
    private fun smooth(x: Float): Float = x * x * (3f - 2f * x)

    private companion object {
        const val STEP_MS = 40L
    }
}
