package dev.geode.render.fluid

import kotlin.math.pow

internal object CurlFlowMath {
    const val MIN_RETENTION = 0.6f

    const val OFF_RETENTION = 0.45f

    const val BASE_BRIGHTNESS = 0.85f

    const val BEAT_BRIGHTNESS = 0.35f

    const val BASE_AMP = 0.55f

    const val BEAT_AMP = 0.9f

    fun beatDrive(
        beatEnvelope: Float,
        beatResponse: Float,
    ): Float = beatEnvelope * beatResponse.coerceIn(0f, 2f)

    fun fieldAmp(
        audioDrive: Float,
        beatDrive: Float,
    ): Float =
        BASE_AMP *
            audioDrive.coerceIn(FluidMath.MIN_AUDIO_DRIVE, FluidMath.MAX_AUDIO_DRIVE) *
            (1f + beatDrive.coerceIn(0f, 2f) * BEAT_AMP)

    fun retention(
        trailLength: Float,
        trails: Boolean = true,
    ): Float {
        if (!trails) return OFF_RETENTION
        val t = trailLength.coerceIn(0f, 1f)
        return MIN_RETENTION + (1f - MIN_RETENTION) * t
    }

    fun fadeAlpha(
        trails: Boolean,
        trailLength: Float,
        dt: Float,
    ): Float = 1f - (retention(trailLength, trails) * 0.97f).pow(dt * 60f)

    fun warpDecay(
        retention: Float,
        dt: Float,
    ): Float = (retention * 0.97f + 0.02f).coerceIn(0f, 0.99f).pow(dt * 60f)

    fun particleBrightness(beatEnvelope: Float): Float = BASE_BRIGHTNESS + beatEnvelope.coerceIn(0f, 1f) * BEAT_BRIGHTNESS
}
