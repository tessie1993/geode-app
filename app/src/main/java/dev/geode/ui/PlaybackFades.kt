package dev.geode.ui

import androidx.annotation.OptIn
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.common.util.Util
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

internal class PlaybackFades(
    private val scope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        val player: Player
        val fadeMs: Int

        fun stopLiveInput()
    }

    @Volatile
    private var sleepVolume = 1f

    @Volatile
    private var fadeVolume = 1f

    private var fadeJob: Job? = null

    val sleepFadeHook: (Float) -> Unit = { v ->
        sleepVolume = v
        applyVolume()
    }

    private fun applyVolume() {
        host.player.volume = (sleepVolume * fadeVolume).coerceIn(0f, 1f)
    }

    private fun fadeThen(
        from: Float,
        to: Float,
        then: () -> Unit,
    ) {
        val durationMs = host.fadeMs
        fadeJob?.cancel()
        if (durationMs <= 0) {
            fadeVolume = to
            applyVolume()
            then()
            return
        }
        lateinit var self: Job
        self =
            scope.launch {
                val steps = (durationMs / FADE_STEP_MS).coerceAtLeast(1)
                // Set the start volume once, then step from 1..steps so the fade takes
                // exactly `steps` intervals (durationMs) instead of steps + 1.
                fadeVolume = from
                applyVolume()
                for (i in 1..steps) {
                    delay(FADE_STEP_MS)
                    fadeVolume = from + (to - from) * (i.toFloat() / steps)
                    applyVolume()
                }
                fadeVolume = to
                applyVolume()
                then()
                // `then()` (e.g. skipFaded's nested fade-in) may have started a new fadeJob;
                // only clear the field if it still points at this coroutine.
                if (fadeJob === self) fadeJob = null
            }
        fadeJob = self
    }

    @OptIn(UnstableApi::class)
    fun togglePlayPauseFaded() {
        host.stopLiveInput()
        val player = host.player
        if (player.isPlaying) {
            fadeThen(fadeVolume, 0f) { player.pause() }
        } else {
            fadeVolume = 0f
            applyVolume()
            fadeThen(0f, 1f) {}
            // play() alone is a no-op once the track has ENDED (repeat off); this rewinds first.
            Util.handlePlayButtonAction(player)
        }
    }

    fun skipFaded(action: () -> Unit) {
        if (host.fadeMs <= 0 || !host.player.isPlaying) {
            action()
            return
        }
        fadeThen(fadeVolume, 0f) {
            action()
            fadeThen(0f, 1f) {}
        }
    }

    fun ensureAudibleAfterExternalPlay() {
        if (fadeVolume < 1f && fadeJob?.isActive != true) fadeThen(fadeVolume, 1f) {}
    }

    private companion object {
        const val FADE_STEP_MS = 25L
    }
}
