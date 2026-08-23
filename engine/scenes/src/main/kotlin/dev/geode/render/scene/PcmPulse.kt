package dev.geode.render.scene

import kotlin.math.abs

internal class PcmPulse(
    private val decayPerSecond: Float = 4f,
    private val ceiling: Float = 1.5f,
) {
    private var level = 0f

    fun accept(
        samples: FloatArray,
        count: Int,
    ) {
        var peak = 0f
        var i = 0
        while (i < count) {
            val s = samples[i]
            if (s.isFinite()) {
                val a = abs(s)
                if (a > peak) peak = a
            }
            i++
        }
        if (peak > level) level = peak.coerceAtMost(ceiling)
    }

    fun tick(dt: Float): Float {
        val out = level
        level = (level - dt * decayPerSecond).coerceAtLeast(0f)
        return out
    }
}
