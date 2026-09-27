package dev.geode.viz

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF

/** Which corner of the frame the watermark is anchored to. */
internal enum class WatermarkCorner {
    TOP_LEFT,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_RIGHT,
}

internal data class WatermarkOptions(
    val enabled: Boolean = false,
    val uri: String? = null,
    val corner: WatermarkCorner = WatermarkCorner.BOTTOM_RIGHT,
    val sizeFraction: Float = 0.16f,
    val opacity: Float = 0.8f,
) {
    companion object {
        const val MIN_SIZE_FRACTION = 0.08f
        const val MAX_SIZE_FRACTION = 0.3f
        const val MIN_OPACITY = 0.2f
        const val MAX_OPACITY = 1.0f
    }
}

/**
 * Draws a user-picked logo/watermark image anchored to one corner of the frame, sized as a
 * fraction of the frame's short side and alpha-blended by [WatermarkOptions.opacity].
 *
 * [bitmap] is decoded once by `OverlayController` and cached there; this layer never decodes,
 * scales down further or recycles it - it only reads pixels while drawing.
 */
internal data class WatermarkLayer(
    private val options: WatermarkOptions,
    private val bitmap: Bitmap?,
) : OverlayLayer {
    override val enabled: Boolean
        get() = options.enabled && bitmap != null && !bitmap.isRecycled

    override fun draw(
        canvas: Canvas,
        frame: OverlayFrame,
    ) {
        if (!enabled) return
        val bmp = bitmap ?: return
        val w = frame.width.toFloat()
        val h = frame.height.toFloat()
        val short = minOf(w, h)
        if (short <= 0f || bmp.width <= 0 || bmp.height <= 0) return

        val margin = short * MARGIN_FRACTION
        val fraction = options.sizeFraction.coerceIn(WatermarkOptions.MIN_SIZE_FRACTION, WatermarkOptions.MAX_SIZE_FRACTION)
        val targetSize = short * fraction
        val scale = targetSize / maxOf(bmp.width, bmp.height).toFloat()
        val dstW = bmp.width * scale
        val dstH = bmp.height * scale

        val left =
            when (options.corner) {
                WatermarkCorner.TOP_LEFT, WatermarkCorner.BOTTOM_LEFT -> margin
                WatermarkCorner.TOP_RIGHT, WatermarkCorner.BOTTOM_RIGHT -> w - margin - dstW
            }
        val top =
            when (options.corner) {
                WatermarkCorner.TOP_LEFT, WatermarkCorner.TOP_RIGHT -> margin
                WatermarkCorner.BOTTOM_LEFT, WatermarkCorner.BOTTOM_RIGHT -> h - margin - dstH
            }

        val opacity = options.opacity.coerceIn(WatermarkOptions.MIN_OPACITY, WatermarkOptions.MAX_OPACITY)
        val paint =
            Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG).apply {
                alpha = (opacity * 255f).toInt()
            }
        canvas.drawBitmap(
            bmp,
            Rect(0, 0, bmp.width, bmp.height),
            RectF(left, top, left + dstW, top + dstH),
            paint,
        )
    }

    private companion object {
        const val MARGIN_FRACTION = 0.035f
    }
}
