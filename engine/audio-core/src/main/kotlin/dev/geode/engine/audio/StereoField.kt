package dev.geode.engine.audio

import kotlin.math.sqrt

object StereoField {
    data class Reading(
        val width: Float,
        val correlation: Float,
        val pan: Float,
    )

    val MONO = Reading(width = 0f, correlation = 1f, pan = 0f)

    fun of(
        mid: FloatArray,
        side: FloatArray,
        count: Int = minOf(mid.size, side.size),
    ): Reading = Reading(width(mid, side, count), correlation(mid, side, count), pan(mid, side, count))

    fun correlation(
        mid: FloatArray,
        side: FloatArray,
        count: Int = minOf(mid.size, side.size),
    ): Float {
        var mm = 0f
        var ss = 0f
        var ms = 0f
        for (i in 0 until count) {
            val m = mid[i]
            val s = side[i]
            mm += m * m
            ss += s * s
            ms += m * s
        }
        val ll = mm + 2f * ms + ss
        val rr = mm - 2f * ms + ss
        val denom = sqrt(ll.coerceAtLeast(0f) * rr.coerceAtLeast(0f))
        if (denom <= SILENCE) return 1f
        return ((mm - ss) / denom).coerceIn(-1f, 1f)
    }

    fun width(
        mid: FloatArray,
        side: FloatArray,
        count: Int = minOf(mid.size, side.size),
    ): Float {
        if (count <= 0) return 0f
        var mm = 0f
        var ss = 0f
        for (i in 0 until count) {
            mm += mid[i] * mid[i]
            ss += side[i] * side[i]
        }
        val m = sqrt(mm / count)
        val s = sqrt(ss / count)
        val total = m + s
        if (total <= SILENCE) return 0f
        return (s / total).coerceIn(0f, 1f)
    }

    fun pan(
        mid: FloatArray,
        side: FloatArray,
        count: Int = minOf(mid.size, side.size),
    ): Float {
        if (count <= 0) return 0f
        var mm = 0f
        var ss = 0f
        var ms = 0f
        for (i in 0 until count) {
            val m = mid[i]
            val s = side[i]
            mm += m * m
            ss += s * s
            ms += m * s
        }
        val left = sqrt((mm + 2f * ms + ss).coerceAtLeast(0f) / count)
        val right = sqrt((mm - 2f * ms + ss).coerceAtLeast(0f) / count)
        val total = left + right
        if (total <= SILENCE) return 0f
        return ((right - left) / total).coerceIn(-1f, 1f)
    }

    private const val SILENCE = 1e-9f
}
