package dev.geode.analysis

import kotlin.math.pow
import kotlin.math.roundToInt

object PlaybackMath {
    const val SLEEP_FADE_MS = 3_000L

    fun semitonesToRatio(semitones: Float): Float = 2.0.pow(semitones / 12.0).toFloat()

    fun sleepFadeVolume(
        remainingMs: Long,
        fadeMs: Long = SLEEP_FADE_MS,
    ): Float {
        if (fadeMs <= 0L) return if (remainingMs > 0L) 1f else 0f
        return (remainingMs.toFloat() / fadeMs).coerceIn(0f, 1f)
    }

    fun formatCountdown(remainingMs: Long): String {
        val totalSec = (remainingMs.coerceAtLeast(0L) + 999L) / 1000L
        return "%d:%02d".format(totalSec / 60, totalSec % 60)
    }

    fun snap(
        value: Float,
        step: Float,
    ): Float = if (step <= 0f) value else (value / step).roundToInt() * step
}
