package dev.geode.analysis

import kotlin.math.pow
import kotlin.math.roundToInt

object PlaybackMath {
    fun semitonesToRatio(semitones: Float): Float = 2.0.pow(semitones / 12.0).toFloat()

    fun formatCountdown(remainingMs: Long): String {
        val totalSec = (remainingMs.coerceAtLeast(0L) + 999L) / 1000L
        return "%d:%02d".format(totalSec / 60, totalSec % 60)
    }

    fun snap(
        value: Float,
        step: Float,
    ): Float = if (step <= 0f) value else (value / step).roundToInt() * step
}
