package dev.geode.viz

import android.graphics.Bitmap
import android.graphics.Canvas

/**
 * Composes [layers] into one ARGB pixel buffer, the shape [dev.geode.render.bridge.NativeViz]'s
 * `setOverlay` expects.
 *
 * Not thread-safe by itself — callers (see `OverlayController`) confine it to a single thread,
 * the same way every other cross-thread render input in this codebase is confined rather than
 * synchronized.
 */
internal class OverlayComposer {
    var layers: List<OverlayLayer> = emptyList()

    private var cached: Cache? = null

    private class Cache(
        val width: Int,
        val height: Int,
        val layers: List<OverlayLayer>,
        val frame: OverlayFrame,
        val pixels: IntArray,
    )

    /**
     * Composes [layers] at [width]x[height] for [frame], reusing the last result when nothing
     * that would change the pixels has changed. Returns null when no layer has anything to draw,
     * so the caller clears the native overlay instead of uploading a transparent one.
     */
    fun compose(
        width: Int,
        height: Int,
        frame: OverlayFrame,
    ): IntArray? {
        val active = layers.filter { it.enabled }
        if (active.isEmpty() || width <= 0 || height <= 0) {
            cached = null
            return null
        }
        val fullFrame = frame.copy(width = width, height = height)
        cached?.let {
            val sameSize = it.width == width && it.height == height
            if (sameSize && it.layers == active && it.frame == fullFrame) {
                return it.pixels
            }
        }
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        active.forEach { it.draw(canvas, fullFrame) }
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        bitmap.recycle()
        // Bitmap.getPixels() always returns non-premultiplied (straight) alpha - per its own
        // javadoc - regardless of how the bitmap is stored internally. NativeViz.setOverlay and
        // the native compositor's blend (GL_ONE, GL_ONE_MINUS_SRC_ALPHA) both need premultiplied
        // colour channels, so every semi-transparent pixel (anti-aliased text, the drop shadow,
        // the artwork's rounded corners) has to be converted here or it composites too bright.
        premultiply(pixels)
        cached = Cache(width, height, active, fullFrame, pixels)
        return pixels
    }

    /**
     * Straight-alpha ARGB in place -> premultiplied-alpha ARGB, matching
     * [dev.geode.render.bridge.NativeViz.setOverlay]'s contract.
     */
    private fun premultiply(pixels: IntArray) {
        for (i in pixels.indices) {
            val p = pixels[i]
            val a = (p ushr 24) and 0xFF
            if (a == 0xFF || a == 0) continue
            val r = (p ushr 16) and 0xFF
            val g = (p ushr 8) and 0xFF
            val b = p and 0xFF
            pixels[i] = (a shl 24) or ((r * a / 255) shl 16) or ((g * a / 255) shl 8) or (b * a / 255)
        }
    }
}
