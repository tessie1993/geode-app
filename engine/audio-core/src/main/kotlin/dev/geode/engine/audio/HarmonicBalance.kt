package dev.geode.engine.audio

import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min

class HarmonicBalance(
    private val binCount: Int,
    hopRateHz: Float,
    historySeconds: Float = 0.2f,
    smoothingSeconds: Float = 0.25f,
) {
    init {
        require(binCount > 0) { "binCount must be positive, was $binCount" }
        require(hopRateHz > 0f) { "hopRateHz must be positive, was $hopRateHz" }
    }

    private val historyPole = 1f - exp(-1f / (historySeconds * hopRateHz))
    private val smoothingPole = 1f - exp(-1f / (smoothingSeconds * hopRateHz))
    private val history = FloatArray(binCount)

    var balance: Float = UNDECIDED
        private set

    fun step(magnitudes: FloatArray) {
        require(magnitudes.size == binCount) { "expected $binCount bins, got ${magnitudes.size}" }
        var harmonic = 0.0
        var percussive = 0.0
        for (k in 0 until binCount) {
            val m = magnitudes[k]
            val h = history[k]
            harmonic += min(m, h).toDouble()
            percussive += max(m - h, 0f).toDouble()
            history[k] = h + (m - h) * historyPole
        }
        val total = harmonic + percussive
        if (total <= SILENCE) return
        val instantaneous = (harmonic / total).toFloat()
        balance += (instantaneous - balance) * smoothingPole
    }

    fun reset() {
        history.fill(0f)
        balance = UNDECIDED
    }

    companion object {
        const val UNDECIDED = 0.5f

        private const val SILENCE = 1e-7
    }
}
