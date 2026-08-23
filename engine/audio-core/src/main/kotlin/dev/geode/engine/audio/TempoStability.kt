package dev.geode.engine.audio

import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.ln

class TempoStability(
    hopRateHz: Float,
    meanSeconds: Float = 1f,
    deviationSeconds: Float = 2f,
    silenceSeconds: Float = 1f,
) {
    init {
        require(hopRateHz > 0f) { "hopRateHz must be positive, was $hopRateHz" }
    }

    private val meanPole = 1f - exp(-1f / (meanSeconds * hopRateHz))
    private val devPole = 1f - exp(-1f / (deviationSeconds * hopRateHz))
    private val silencePole = 1f - exp(-1f / (silenceSeconds * hopRateHz))

    private var mean = 0f
    private var dev = SCALE_OCTAVES
    private var seeded = false

    var value: Float = 0f
        private set

    fun step(bpm: Float) {
        if (bpm <= 0f) {
            dev += (SCALE_OCTAVES - dev) * silencePole
            value = if (seeded) (1f - dev / SCALE_OCTAVES).coerceIn(0f, 1f) else 0f
            return
        }
        val x = ln(bpm) / LN_2
        if (!seeded) {
            seeded = true
            mean = x
            dev = SCALE_OCTAVES
        }
        mean += (x - mean) * meanPole
        dev += (abs(x - mean) - dev) * devPole
        value = (1f - dev / SCALE_OCTAVES).coerceIn(0f, 1f)
    }

    fun reset() {
        mean = 0f
        dev = SCALE_OCTAVES
        seeded = false
        value = 0f
    }

    companion object {
        const val SCALE_OCTAVES: Float = 0.25f

        private val LN_2 = ln(2.0).toFloat()
    }
}
