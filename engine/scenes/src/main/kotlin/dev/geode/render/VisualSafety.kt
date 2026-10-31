package dev.geode.render

/**
 * The flash and luminance clamp, as far as Kotlin still needs it.
 *
 * The clamp itself is native (core/viz/VisualSafety: `apply`, `beatMinIntervalMs`, `layerMix`)
 * and runs after every scene, preset and parameter on preview and export alike; it is not a
 * setting. What stays here is the constant the UI's slider ranges quote. The cut-to-fade
 * transition rule is applied natively by Renderer::setTransition. ADR 0003 removed the unused
 * Kotlin copies of the rest.
 */
object VisualSafety {
    /** Ceiling on how far luminance may swing within one flash. Mirrors `kMaxFlashDepth`. */
    const val MAX_FLASH_DEPTH = 0.25f
}
