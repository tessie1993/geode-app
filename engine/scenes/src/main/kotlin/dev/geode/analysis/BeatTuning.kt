package dev.geode.analysis

import kotlin.math.exp
import kotlin.math.ln

object BeatTuning {
    const val SENSITIVITY_MIN: Float = 1.5f

    const val SENSITIVITY_MAX: Float = 8f

    const val SENSITIVITY_DEFAULT: Float = 3f

    const val SLOW_SENSITIVITY: Float = 5.5f

    const val INTERVAL_MS_MIN: Float = 200f

    const val INTERVAL_MS_MAX: Float = 1200f

    const val INTERVAL_MS_DEFAULT: Float = 1000f / 3f

    const val SLOW_INTERVAL_MS: Float = 700f

    const val REFERENCE_HOP_HZ: Float = 62.5f

    fun clampSensitivity(value: Float): Float = value.coerceIn(SENSITIVITY_MIN, SENSITIVITY_MAX)

    fun clampIntervalMs(value: Float): Float = value.coerceIn(INTERVAL_MS_MIN, INTERVAL_MS_MAX)

    @Suppress("ReturnCount")
    fun envelopeSeconds(perTickFraction: Float): Float {
        val f = perTickFraction.coerceIn(0f, 1f)
        if (f >= 1f) return 0f
        if (f <= 0f) return MAX_ENVELOPE_SECONDS
        val dt = 1f / REFERENCE_HOP_HZ
        return (-dt / ln(1f - f)).coerceIn(0f, MAX_ENVELOPE_SECONDS)
    }

    fun envelopeFraction(seconds: Float): Float {
        if (seconds <= 0f) return 1f
        return (1f - exp(-1f / (REFERENCE_HOP_HZ * seconds))).coerceIn(0f, 1f)
    }

    private const val MAX_ENVELOPE_SECONDS = 4f
}
