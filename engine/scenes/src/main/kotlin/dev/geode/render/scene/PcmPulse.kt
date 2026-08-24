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
        // Clamped to the array, not trusted from [count]. Every other PCM consumer here already
        // does this - PcmRow.fill coerces into source.size, ShaderScene and BeamScene into their
        // own buffer, and the JNI bridge re-reads GetArrayLength for the same reason - and this
        // was the one path that indexed straight to a caller-supplied length. Six scenes route
        // into it, so a short read anywhere upstream would be six crashes rather than quiet audio.
        val n = count.coerceAtMost(samples.size)
        var peak = 0f
        var i = 0
        while (i < n) {
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
