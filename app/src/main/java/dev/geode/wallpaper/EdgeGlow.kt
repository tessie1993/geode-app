package dev.geode.wallpaper

/**
 * The edge look's options ([WallpaperLook.EDGE]): the visual stays along the screen's rounded rim and fades
 * to black inward, the band breathing with the music. All of it, the GL, the band and corner maths and
 * the sound envelope, is native (`core/viz/frame/EdgeGlowPass`, `EdgeGlowMath.hpp`, #439); this is only
 * what the wallpaper hands it through `geode_viz_set_edge_glow`.
 *
 * The window is the part of the surface on screen, in surface pixels, y down: a wallpaper surface can be
 * wider than the display so the launcher can pan it, and the edge belongs on the screen's edge, not on
 * the surface's, which is off screen on the middle pages. A width or height of 0 is the whole surface.
 */
data class EdgeGlow(
    val enabled: Boolean = false,
    val left: Float = 0f,
    val top: Float = 0f,
    val width: Float = 0f,
    val height: Float = 0f,
) {
    /** In the order of `GEODE_EDGE_GLOW_FLOATS` (`core/api/geode_api.h`). */
    fun toFloatArray(): FloatArray = floatArrayOf(if (enabled) 1f else 0f, left, top, width, height)
}
