package dev.geode.render.scene

object ParticleLook {
    const val STRETCH_SECONDS: Float = 0.0025f

    private const val GLOW_BASE: Float = 0.85f
    private const val GLOW_PER_BLOOM: Float = 1.2f

    private const val REFERENCE_HEIGHT_PX: Float = 1080f

    fun glow(bloom: Float): Float = GLOW_BASE + bloom.coerceIn(0f, 1f) * GLOW_PER_BLOOM

    fun dpiScale(viewportHeightPx: Int): Float = (viewportHeightPx.coerceAtLeast(1) / REFERENCE_HEIGHT_PX).coerceIn(0.75f, 2.5f)
}
