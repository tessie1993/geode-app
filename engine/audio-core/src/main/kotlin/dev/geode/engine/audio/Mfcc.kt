package dev.geode.engine.audio

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.sqrt

class Mfcc(
    private val melCount: Int,
    val count: Int = 13,
) {
    init {
        require(count in 1..melCount) { "need 1..$melCount coefficients, got $count" }
    }

    private val basis =
        Array(count) { c ->
            val scale = if (c == 0) sqrt(1.0 / melCount) else sqrt(2.0 / melCount)
            DoubleArray(melCount) { n -> scale * cos(PI * c * (2 * n + 1) / (2.0 * melCount)) }
        }

    private val logMel = DoubleArray(melCount)
    private val previous = FloatArray(count)
    private var hasPrevious = false

    val coefficients: FloatArray = FloatArray(count)

    val delta: FloatArray = FloatArray(count)

    var timbreFlux: Float = 0f
        private set

    fun compute(melPower: FloatArray) {
        require(melPower.size == melCount) { "expected $melCount mels, got ${melPower.size}" }
        for (m in 0 until melCount) {
            logMel[m] = 10.0 * log10(max(melPower[m].toDouble(), LOG_POWER_FLOOR))
        }
        for (c in 0 until count) {
            val row = basis[c]
            var acc = 0.0
            for (m in 0 until melCount) acc += row[m] * logMel[m]
            coefficients[c] = acc.toFloat()
        }
        if (hasPrevious) {
            var sq = 0.0
            for (c in 0 until count) {
                val d = coefficients[c] - previous[c]
                delta[c] = d
                if (c > 0) sq += d.toDouble() * d
            }
            timbreFlux = sqrt(sq).toFloat()
        } else {
            delta.fill(0f)
            timbreFlux = 0f
            hasPrevious = true
        }
        coefficients.copyInto(previous)
    }

    fun reset() {
        hasPrevious = false
        previous.fill(0f)
        coefficients.fill(0f)
        delta.fill(0f)
        timbreFlux = 0f
    }

    companion object {
        const val LOG_POWER_FLOOR: Double = 1e-10
    }
}
