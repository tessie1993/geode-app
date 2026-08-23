package dev.geode.render.fluid

import kotlin.math.floor

internal object FluidHue {
    const val MIN_HUE_RANGE = 0.1f

    const val MAX_HUE_RANGE = 1.5f

    const val MAX_PALETTE_CYCLE = 2f

    fun range(hueRange: Float): Float = hueRange.coerceIn(MIN_HUE_RANGE, MAX_HUE_RANGE)

    fun wrap01(hue: Float): Float {
        val h = hue - floor(hue)
        return if (h >= 1f) 0f else h
    }

    fun base(paletteBase: Float): Float = wrap01(paletteBase)

    fun paletteCycleSpeed(fluidPaletteCycleSpeed: Float): Float = fluidPaletteCycleSpeed.coerceIn(0f, MAX_PALETTE_CYCLE)

    fun span(
        hueRange: Float,
        paletteRange: Float,
    ): Float = range(hueRange) * paletteRange.coerceIn(0f, 1f)

    fun hsv(
        h: Float,
        s: Float,
        v: Float,
    ): Triple<Float, Float, Float> {
        val out = FloatArray(3)
        hsv(h, s, v, out)
        return Triple(out[0], out[1], out[2])
    }

    fun hsv(
        h: Float,
        s: Float,
        v: Float,
        out: FloatArray,
    ) {
        val hh = wrap01(h)
        val sextant = hh * 6f
        val i = sextant.toInt() % 6
        val fr = sextant - sextant.toInt()
        val p = v * (1f - s)
        val q = v * (1f - fr * s)
        val t = v * (1f - (1f - fr) * s)
        when (i) {
            0 -> set(out, v, t, p)
            1 -> set(out, q, v, p)
            2 -> set(out, p, v, t)
            3 -> set(out, p, q, v)
            4 -> set(out, t, p, v)
            else -> set(out, v, p, q)
        }
    }

    private fun set(
        out: FloatArray,
        r: Float,
        g: Float,
        b: Float,
    ) {
        out[0] = r
        out[1] = g
        out[2] = b
    }

    fun rgb(
        hue: Float,
        saturation: Float,
    ): Triple<Float, Float, Float> = hsv(hue, saturation.coerceIn(0f, 1f), 1f)

    fun rgb(
        hue: Float,
        saturation: Float,
        out: FloatArray,
    ) = hsv(hue, saturation.coerceIn(0f, 1f), 1f, out)
}
