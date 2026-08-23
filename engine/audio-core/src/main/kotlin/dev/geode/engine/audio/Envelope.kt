package dev.geode.engine.audio

import kotlin.math.exp

class Envelope(
    @Volatile var attackSeconds: Float,
    @Volatile var releaseSeconds: Float,
) {
    var value: Float = 0f
        private set

    fun step(
        target: Float,
        dtSeconds: Float,
    ): Float {
        if (dtSeconds <= 0f) return value
        val tau = if (target > value) attackSeconds else releaseSeconds
        value =
            if (tau <= 0f) {
                target
            } else {
                val k = (1f - exp(-dtSeconds / tau)).coerceIn(0f, 1f)
                value + (target - value) * k
            }
        return value
    }

    fun primeTo(level: Float) {
        value = level
    }

    fun reset() {
        value = 0f
    }
}
