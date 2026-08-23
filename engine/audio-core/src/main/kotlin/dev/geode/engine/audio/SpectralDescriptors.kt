package dev.geode.engine.audio

import kotlin.math.ln
import kotlin.math.max
import kotlin.math.sqrt

object SpectralDescriptors {
    const val SILENCE_TOTAL = 1e-12

    private const val POWER_FLOOR = 1e-10

    fun centroidHz(
        magnitudes: FloatArray,
        binHz: Double,
    ): Double {
        var weighted = 0.0
        var total = 0.0
        for (k in magnitudes.indices) {
            val m = magnitudes[k].toDouble()
            weighted += k * binHz * m
            total += m
        }
        return if (total > SILENCE_TOTAL) weighted / total else 0.0
    }

    fun bandwidthHz(
        magnitudes: FloatArray,
        binHz: Double,
        centroidHz: Double,
    ): Double {
        var deviation = 0.0
        var total = 0.0
        for (k in magnitudes.indices) {
            val m = magnitudes[k].toDouble()
            val offset = k * binHz - centroidHz
            deviation += m * offset * offset
            total += m
        }
        return if (total > SILENCE_TOTAL) sqrt(deviation / total) else 0.0
    }

    fun rolloffHz(
        magnitudes: FloatArray,
        binHz: Double,
        fraction: Double = DEFAULT_ROLLOFF,
    ): Double {
        require(fraction > 0.0 && fraction <= 1.0) { "fraction must be in (0, 1], was $fraction" }
        var total = 0.0
        for (m in magnitudes) total += m.toDouble()
        if (total <= SILENCE_TOTAL) return 0.0

        val threshold = fraction * total
        var running = 0.0
        for (k in magnitudes.indices) {
            running += magnitudes[k].toDouble()
            if (running >= threshold) return k * binHz
        }
        return (magnitudes.size - 1) * binHz
    }

    fun flatness(magnitudes: FloatArray): Double {
        if (magnitudes.isEmpty()) return 0.0
        var logSum = 0.0
        var sum = 0.0
        for (m in magnitudes) {
            val power = max(m.toDouble() * m, POWER_FLOOR)
            logSum += ln(power)
            sum += power
        }
        val n = magnitudes.size
        return kotlin.math.exp(logSum / n) / (sum / n)
    }

    const val DEFAULT_ROLLOFF = 0.85
}
