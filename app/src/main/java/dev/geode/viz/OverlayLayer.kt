package dev.geode.viz

import android.graphics.Bitmap
import android.graphics.Canvas

/**
 * Everything an [OverlayLayer] needs to draw one composed frame: the target size (the live
 * surface, or an export's frame size) and the track state at [positionMs].
 */
internal data class OverlayFrame(
    val width: Int,
    val height: Int,
    val positionMs: Long,
    val title: String?,
    val artist: String?,
    val artwork: Bitmap?,
)

/**
 * One drawable element of the composited overlay (cover art + title, and whatever future units
 * add — lyrics, a progress bar, a watermark). Kept deliberately minimal: [OverlayComposer] only
 * needs to know whether a layer has anything to draw and how to draw it.
 */
internal interface OverlayLayer {
    val enabled: Boolean

    fun draw(
        canvas: Canvas,
        frame: OverlayFrame,
    )
}
