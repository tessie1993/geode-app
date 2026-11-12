package dev.geode.ui

import kotlin.math.roundToInt

object ColorDerive {
    fun scaleSaturation(
        argb: Int,
        factor: Float,
    ): Int {
        if (factor == 1f) return argb
        val r = (argb ushr 16) and 0xFF
        val g = (argb ushr 8) and 0xFF
        val b = argb and 0xFF
        val luma = 0.299f * r + 0.587f * g + 0.114f * b

        fun ch(c: Int): Int = (luma + (c - luma) * factor).roundToInt().coerceIn(0, 255)
        return (argb and 0xFF000000.toInt()) or (ch(r) shl 16) or (ch(g) shl 8) or ch(b)
    }
}
