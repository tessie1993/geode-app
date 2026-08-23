package dev.geode.render

import dev.geode.render.scene.SceneParams

/**
 * The flash and luminance clamp.
 *
 * This is not a setting. There is no mode, no off switch and no strict tier: every scene, every
 * preset, every imported `.milk` file and every parameter combination passes through the same
 * numbers, on preview and on export alike, because the clamp is applied after all of them. A
 * preview that is safe therefore cannot produce an unsafe file — same clamp, same code, same
 * numbers.
 *
 * The threshold is the accepted one: no more than three flashes in any one second, with the
 * luminance change of anything faster held under [MAX_FLASH_DEPTH].
 *
 * Reduced motion is deliberately *not* part of this. It is a vestibular accessibility preference
 * about how much the picture moves, not a photosensitivity guard, so it stays a choice the user
 * makes while the flash clamp stays one they cannot.
 */
object VisualSafety {
    /** No more than three flashes in any one second. The rule, not a default. */
    const val WCAG_FLASHES_PER_SECOND = 3f

    /** Ceiling on how far luminance may swing within one flash. */
    const val MAX_FLASH_DEPTH = 0.25f

    const val STROBE_SHADER_DEPTH = 0.85f

    const val FLASH_SHADER_DEPTH = 0.6f

    const val REDUCED_MOTION_SCALE = 0.4f

    /**
     * Clamps [p] to the flash limit, then optionally slows it for reduced motion.
     *
     * Inversion and solarize are left alone on purpose. A statically inverted picture is not a
     * flash — the hazard is toggling it quickly, and the rate limits below already bound how fast
     * luminance is allowed to move. Forcing them off would only produce a dead control.
     */
    fun apply(
        p: SceneParams,
        reducedMotion: Boolean = false,
    ): SceneParams {
        var out =
            p.copy(
                strobe = p.strobe.coerceIn(0f, MAX_FLASH_DEPTH / STROBE_SHADER_DEPTH),
                flash = p.flash.coerceIn(0f, MAX_FLASH_DEPTH / FLASH_SHADER_DEPTH),
                glitch = p.glitch.coerceAtMost(MAX_FLASH_DEPTH),
                bloom = p.bloom.coerceAtMost(MAX_FLASH_DEPTH),
                brightness = p.brightness.coerceIn(0f, 1f + MAX_FLASH_DEPTH),
                intensity = p.intensity.coerceIn(0f, 1f + MAX_FLASH_DEPTH),
                contrast = p.contrast.coerceIn(0f, 1f + MAX_FLASH_DEPTH),
            )
        if (reducedMotion) {
            out =
                out.copy(
                    speed = out.speed * REDUCED_MOTION_SCALE,
                    shake = out.shake * REDUCED_MOTION_SCALE,
                    sway = out.sway * REDUCED_MOTION_SCALE,
                    driftX = out.driftX * REDUCED_MOTION_SCALE,
                    driftY = out.driftY * REDUCED_MOTION_SCALE,
                    rotation = out.rotation * REDUCED_MOTION_SCALE,
                    turbulence = out.turbulence * REDUCED_MOTION_SCALE,
                    pulse = out.pulse * REDUCED_MOTION_SCALE,
                    cycleSpeed = out.cycleSpeed * REDUCED_MOTION_SCALE,
                    endlessZoomSpeed = out.endlessZoomSpeed * REDUCED_MOTION_SCALE,
                )
        }
        return out
    }

    fun flashImpulse(
        flash: Float,
        beatImpulse: Float,
    ): Float = flash * beatImpulse * FLASH_SHADER_DEPTH

    /** The strobe rate any scene may run at. Fixed at the limit. */
    fun strobeHz(): Float = WCAG_FLASHES_PER_SECOND

    /** Holds an LFO driving a luminance target down to the flash limit. */
    fun limitLfoRate(
        rateHz: Float,
        target: LfoTarget,
    ): Float = if (target.isLuminance) rateHz.coerceAtMost(WCAG_FLASHES_PER_SECOND) else rateHz

    /** Floors the gap between beat-driven hits so they cannot outrun the flash limit. */
    fun beatMinIntervalMs(requestedMs: Float): Float = maxOf(requestedMs, 1000f / WCAG_FLASHES_PER_SECOND)

    /** A hard cut between scenes is a full-frame luminance step, so it always becomes a fade. */
    fun transitionStyle(requested: TransitionStyle): TransitionStyle =
        if (requested == TransitionStyle.CUT) TransitionStyle.FADE else requested

    fun transitionId(requested: String): String =
        if (requested == TransitionStyle.CUT.name.lowercase()) {
            TransitionStyle.FADE.name.lowercase()
        } else {
            requested
        }

    /**
     * Caps how hard a second layer may be blended in.
     *
     * Additive and difference blending both drive large luminance swings at full mix, so both are
     * held to [MAX_FLASH_DEPTH] rather than switched off — the mode stays usable, it just cannot
     * reach flash territory.
     */
    fun layerMix(
        requested: Float,
        mode: BlendMode,
    ): Float {
        val mix = requested.coerceIn(0f, 1f)
        return when (mode) {
            BlendMode.DIFFERENCE, BlendMode.ADD -> minOf(mix, MAX_FLASH_DEPTH)
            else -> mix
        }
    }

    private val LfoTarget.isLuminance: Boolean
        get() =
            when (this) {
                LfoTarget.BRIGHTNESS,
                LfoTarget.INTENSITY,
                LfoTarget.SATURATION,
                LfoTarget.BLOOM,
                LfoTarget.GLITCH,
                LfoTarget.VIGNETTE,
                LfoTarget.COLOR_SHIFT,
                LfoTarget.PALETTE_MIX,
                LfoTarget.TEMPERATURE,
                -> true
                else -> false
            }
}
