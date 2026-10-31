package dev.geode.render

/**
 * The flash and luminance clamp, as far as Kotlin still needs it.
 *
 * The clamp itself is native (core/viz/VisualSafety: `apply`, `beatMinIntervalMs`, `layerMix`)
 * and runs after every scene, preset and parameter on preview and export alike; it is not a
 * setting. What stays here is the constant the UI's slider ranges quote, the beat-gap floor the
 * analysis settings read. The cut-to-fade transition rule is applied natively by Renderer::setTransition.
 * ADR 0003 removed the unused Kotlin copies of the rest.
 */
object VisualSafety {
    /** Ceiling on how far luminance may swing within one flash. Mirrors `kMaxFlashDepth`. */
    const val MAX_FLASH_DEPTH = 0.25f

    /** No more than three flashes in any one second. The rule, not a default. */
    private const val WCAG_FLASHES_PER_SECOND = 3f

    /** Floors the gap between beat-driven hits so they cannot outrun the flash limit. */
    fun beatMinIntervalMs(requestedMs: Float): Float = maxOf(requestedMs, 1000f / WCAG_FLASHES_PER_SECOND)

}
