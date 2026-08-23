package dev.geode.analysis

import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

object ArtPalette {
    const val BINS = 36

    private const val MIN_SATURATION = 0.18f

    private const val MIN_VALUE = 0.12f
    private const val MAX_VALUE = 0.97f

    private const val MIN_SPAN = 0.06f

    data class Extracted(
        val baseHue: Float,
        val span: Float,
        val confidence: Float,
    )

    fun extract(pixels: IntArray): Extracted? {
        if (pixels.isEmpty()) return null
        val weights = FloatArray(BINS)
        var coloured = 0
        for (argb in pixels) {
            val (h, s, v) = hsv(argb)
            if (s < MIN_SATURATION || v < MIN_VALUE || v > MAX_VALUE) continue
            coloured++
            weights[(h * BINS).toInt().coerceIn(0, BINS - 1)] += s * v
        }
        val confidence = coloured.toFloat() / pixels.size
        if (coloured == 0 || weights.all { it <= 0f }) return null

        val peak = weights.indices.maxByOrNull { weights[it] } ?: return null
        val left = weights[(peak - 1 + BINS) % BINS]
        val right = weights[(peak + 1) % BINS]
        val denom = left + weights[peak] + right
        val shift = if (denom > 0f) (right - left) / denom * 0.5f else 0f
        val baseHue = wrap01((peak + 0.5f + shift) / BINS)

        val total = weights.sum()
        val floor = total / BINS * 0.6f
        var reach = 0
        for (step in 1..BINS / 2) {
            val hot =
                weights[(peak + step) % BINS] >= floor ||
                    weights[(peak - step + BINS) % BINS] >= floor
            if (hot) reach = step
        }
        val span = max(MIN_SPAN, (reach * 2f + 1f) / BINS)
        return Extracted(baseHue, min(span, 1f), confidence)
    }

    private fun hsv(argb: Int): Triple<Float, Float, Float> {
        val r = ((argb shr 16) and 0xFF) / 255f
        val g = ((argb shr 8) and 0xFF) / 255f
        val b = (argb and 0xFF) / 255f
        val hi = max(r, max(g, b))
        val lo = min(r, min(g, b))
        val d = hi - lo
        val h =
            when {
                d < 1e-6f -> 0f
                hi == r -> ((g - b) / d / 6f)
                hi == g -> ((b - r) / d + 2f) / 6f
                else -> ((r - g) / d + 4f) / 6f
            }
        val s = if (hi <= 0f) 0f else d / hi
        return Triple(wrap01(h), s, hi)
    }

    private fun wrap01(v: Float): Float {
        val x = v - kotlin.math.floor(v)
        return if (x >= 1f || abs(x) < 1e-7f) 0f else x
    }
}
