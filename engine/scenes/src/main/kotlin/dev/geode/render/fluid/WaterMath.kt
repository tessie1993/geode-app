package dev.geode.render.fluid

internal object WaterMath {
    const val MIN_CATCH_RADIUS = 0.03f
    const val MAX_CATCH_RADIUS = 0.3f

    const val REF_CATCH_RADIUS = 0.12f

    private const val MIN_SPREAD = 0.4f
    private const val MAX_SPREAD = 2.5f

    fun isCatchWell(
        r: Float,
        g: Float,
        b: Float,
    ): Boolean = maxOf(r, g, b) <= 0f

    fun catchWellRadius(catchRadius: Float): Float = catchRadius.coerceIn(MIN_CATCH_RADIUS, MAX_CATCH_RADIUS)

    fun catchWellAmplitude(
        speed: Float,
        catchRadius: Float,
        rippleStrength: Float,
    ): Float {
        val r = catchWellRadius(catchRadius)
        val spread = (REF_CATCH_RADIUS / r).coerceIn(MIN_SPREAD, MAX_SPREAD)
        return -(0.06f + 0.5f * speed.coerceIn(0f, 2f)) * spread * rippleStrength.coerceIn(0f, 2f)
    }

    const val DISPLAY_BRIGHTNESS = 1f

    fun effectiveBrightness(
        brightness: Float,
        intensity: Float,
    ): Float = DISPLAY_BRIGHTNESS * brightness * intensity
}
