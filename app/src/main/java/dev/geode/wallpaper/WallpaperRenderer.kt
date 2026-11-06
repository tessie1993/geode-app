package dev.geode.wallpaper

import android.opengl.GLSurfaceView
import dev.geode.render.VisualizerRenderer

/**
 * The wallpaper's GL renderer: the visual, which in [WallpaperLook.EDGE] also draws the edge glow.
 *
 * The glow is a native frame pass the renderer runs after its composite (`core/viz/frame/EdgeGlowPass`,
 * #439), so this only turns it on and off and tells it where the screen is; both go straight to the
 * native renderer, which latches them for the next frame from any thread.
 */
class WallpaperRenderer(
    private val visual: VisualizerRenderer,
) : GLSurfaceView.Renderer by visual {
    private var glow = EdgeGlow()

    /** Main thread: whether the current look is [WallpaperLook.EDGE]. */
    var edge: Boolean
        get() = glow.enabled
        set(value) = push(glow.copy(enabled = value))

    /** Main thread: the part of the surface on screen, in surface pixels; see [EdgeGlow]. */
    fun setVisibleWindow(
        x: Float,
        y: Float,
        width: Float,
        height: Float,
    ) = push(glow.copy(left = x, top = y, width = width, height = height))

    private fun push(next: EdgeGlow) {
        if (next == glow) return
        glow = next
        visual.setEdgeGlow(next.toFloatArray())
    }
}
