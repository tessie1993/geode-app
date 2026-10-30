package dev.geode.analysis

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

    /**
     * The step the attack/decay settings are defined against. They are stored (prefs, presets,
     * templates) as "the fraction of the way to the target per 16 ms step", so this is the unit of
     * that saved number, converted to seconds once by [envelopeSeconds]. It has nothing to do with
     * the analysis frame rate, which is the stream's (see `AnalysisSession.hpp`).
     */
    private const val SETTING_STEP_SECONDS: Float = 0.016f

    fun clampSensitivity(value: Float): Float = value.coerceIn(SENSITIVITY_MIN, SENSITIVITY_MAX)

    fun clampIntervalMs(value: Float): Float = value.coerceIn(INTERVAL_MS_MIN, INTERVAL_MS_MAX)

    @Suppress("ReturnCount")
    fun envelopeSeconds(perTickFraction: Float): Float {
        val f = perTickFraction.coerceIn(0f, 1f)
        if (f >= 1f) return 0f
        if (f <= 0f) return MAX_ENVELOPE_SECONDS
        return (-SETTING_STEP_SECONDS / ln(1f - f)).coerceIn(0f, MAX_ENVELOPE_SECONDS)
    }

    private const val MAX_ENVELOPE_SECONDS = 4f
}
