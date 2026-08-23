package dev.geode.ui

import kotlin.math.roundToInt

object ColorDerive {
    fun lerpArgb(
        from: Int,
        to: Int,
        t: Float,
    ): Int {
        val f = t.coerceIn(0f, 1f)

        fun ch(shift: Int): Int {
            val a = (from ushr shift) and 0xFF
            val b = (to ushr shift) and 0xFF
            return (a + (b - a) * f).roundToInt().coerceIn(0, 255)
        }
        return (ch(24) shl 24) or (ch(16) shl 16) or (ch(8) shl 8) or ch(0)
    }

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

    fun dim(
        argb: Int,
        amount: Float,
    ): Int {
        val k = 1f - amount.coerceIn(0f, 1f)

        fun ch(shift: Int): Int = (((argb ushr shift) and 0xFF) * k).roundToInt().coerceIn(0, 255)
        return (argb and 0xFF000000.toInt()) or (ch(16) shl 16) or (ch(8) shl 8) or ch(0)
    }
}
