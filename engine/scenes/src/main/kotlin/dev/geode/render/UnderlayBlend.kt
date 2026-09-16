package dev.geode.render

/**
 * Blend function for a full-frame background image drawn UNDER/INTO the scene through
 * [dev.geode.render.bridge.NativeViz.setUnderlay].
 *
 * Ordinals are the wire format for `NativeViz.setUnderlay`'s `blend` int and must stay in lockstep
 * with the source of truth: `CompositePass::uploadUnderlay` (core/viz/CompositePass.cpp) clamps the
 * incoming int to 0..2, and `composite_frag.glsl`'s `uUnderlayBlend` branch reads the same three
 * values — 0 = screen (shows the image through where the scene is black, leaves bright scene
 * pixels alone), 1 = multiply, 2 = add.
 */
enum class UnderlayBlend {
    SCREEN,
    MULTIPLY,
    ADD,
    ;

    companion object {
        fun fromOrdinal(i: Int): UnderlayBlend = entries.getOrElse(i) { SCREEN }
    }
}
